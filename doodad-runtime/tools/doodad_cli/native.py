from __future__ import annotations

import ctypes
import atexit
from dataclasses import dataclass
import hashlib
import json
import struct
import subprocess
import sys
from pathlib import Path

from .contract import DoodadError
from .parallax_contract import (
    document_sha256,
    validate_node_evidence,
)


@dataclass(frozen=True)
class SceneOperation:
    scene_revision: int
    route_generation: int
    scenario_time_ms: int
    cause_kind: int
    cause: bytes
    operation_kind: int
    outcome: int
    operation: bytes
    snapshot_json: str | None


SceneOperationCallback = ctypes.CFUNCTYPE(
    None,
    ctypes.c_void_p,
    ctypes.c_uint64,
    ctypes.c_uint64,
    ctypes.c_uint64,
    ctypes.c_int,
    ctypes.POINTER(ctypes.c_uint8),
    ctypes.c_size_t,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.POINTER(ctypes.c_uint8),
    ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_char),
    ctypes.c_size_t,
)


class NativeHost:
    WIDTH = 240
    HEIGHT = 240
    EVENT_KINDS = {
        "tap": 0,
        "long_press": 1,
        "repeat": 2,
        "value_changing": 3,
        "value_committed": 4,
        "checked_changed": 5,
        "page_changed": 6,
        "dismissed": 7,
        "submit": 8,
        "retry": 9,
        "cancel": 10,
    }
    _shared_library: ctypes.CDLL | None = None
    _shared_library_path: Path | None = None
    _shared_created = False
    _atexit_registered = False

    def __init__(self, project_root: Path):
        self.project_root = project_root
        self.library_path = ensure_native_host(project_root)
        if NativeHost._shared_library is None:
            NativeHost._shared_library = ctypes.CDLL(
                str(self.library_path)
            )
            NativeHost._shared_library_path = self.library_path
        elif NativeHost._shared_library_path != self.library_path:
            raise DoodadError(
                "one process cannot host two native Doodad runtimes"
            )
        self.library = NativeHost._shared_library
        self._configure()
        if not NativeHost._shared_created:
            if not self.library.doodad_host_create():
                raise DoodadError(self.last_error())
            NativeHost._shared_created = True
        if not NativeHost._atexit_registered:
            atexit.register(NativeHost._destroy_shared)
            NativeHost._atexit_registered = True
        self._scene_operations: list[SceneOperation] = []
        self._scene_callback = SceneOperationCallback(
            self._receive_scene_operation
        )
        self.library.doodad_host_set_scene_operation_callback(
            self._scene_callback,
            None,
        )
        self.created = True

    @staticmethod
    def _destroy_shared() -> None:
        if (
            NativeHost._shared_created
            and NativeHost._shared_library is not None
        ):
            NativeHost._shared_library.doodad_host_destroy()
            NativeHost._shared_created = False

    def _configure(self) -> None:
        library = self.library
        library.doodad_host_create.restype = ctypes.c_int
        library.doodad_host_destroy.restype = None
        library.doodad_host_last_error.restype = ctypes.c_char_p
        library.doodad_host_start_wasm.argtypes = [ctypes.c_char_p]
        library.doodad_host_start_wasm.restype = ctypes.c_int
        library.doodad_host_render_now.restype = None
        library.doodad_host_framebuffer.restype = ctypes.POINTER(ctypes.c_uint16)
        library.doodad_host_framebuffer_pixels.restype = ctypes.c_size_t
        library.doodad_host_show_catalog.argtypes = [ctypes.c_int]
        library.doodad_host_show_catalog.restype = None
        library.doodad_host_show_appspec.argtypes = [
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_size_t,
        ]
        library.doodad_host_show_appspec.restype = ctypes.c_int
        library.doodad_host_click_first_action.restype = ctypes.c_int
        library.doodad_host_click_button.argtypes = [ctypes.c_char_p]
        library.doodad_host_click_button.restype = ctypes.c_int
        library.doodad_host_dispatch_semantic_action.argtypes = [
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int32,
            ctypes.c_int,
            ctypes.c_char_p,
        ]
        library.doodad_host_dispatch_semantic_action.restype = ctypes.c_int
        library.doodad_host_node_text.argtypes = [ctypes.c_char_p]
        library.doodad_host_node_text.restype = ctypes.c_char_p
        library.doodad_host_semantic_snapshot.argtypes = [
            ctypes.c_char_p,
            ctypes.c_size_t,
        ]
        library.doodad_host_semantic_snapshot.restype = ctypes.c_size_t
        library.doodad_host_scene_snapshot.argtypes = [
            ctypes.c_char_p,
            ctypes.c_size_t,
        ]
        library.doodad_host_scene_snapshot.restype = ctypes.c_size_t
        library.doodad_host_node_layout_evidence.argtypes = [
            ctypes.c_char_p,
            ctypes.c_size_t,
        ]
        library.doodad_host_node_layout_evidence.restype = ctypes.c_size_t
        library.doodad_host_set_scene_operation_callback.argtypes = [
            SceneOperationCallback,
            ctypes.c_void_p,
        ]
        library.doodad_host_set_scene_operation_callback.restype = None
        library.doodad_host_scene_revision.restype = ctypes.c_uint64
        library.doodad_host_route_generation.restype = ctypes.c_uint64
        library.doodad_host_replay_mount.argtypes = [
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_size_t,
            ctypes.c_uint64,
        ]
        library.doodad_host_replay_mount.restype = ctypes.c_int
        library.doodad_host_replay_command_batch.argtypes = [
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_size_t,
            ctypes.c_uint64,
        ]
        library.doodad_host_replay_command_batch.restype = ctypes.c_int
        library.doodad_host_wasm_call_count.restype = ctypes.c_uint64
        library.doodad_host_mounted_node_count.restype = ctypes.c_size_t
        library.doodad_host_mounted_event_count.restype = ctypes.c_size_t
        library.doodad_host_lvgl_object_count.restype = ctypes.c_size_t
        library.doodad_host_lvgl_max_depth.restype = ctypes.c_size_t
        library.doodad_host_semantic_event_count.restype = ctypes.c_uint64
        library.doodad_host_provider_request_count.restype = ctypes.c_uint64
        library.doodad_host_set_display_awake.argtypes = [ctypes.c_int]
        library.doodad_host_set_display_awake.restype = None
        library.doodad_host_display_awake.restype = ctypes.c_int
        library.doodad_host_advance_time.argtypes = [ctypes.c_uint64]
        library.doodad_host_advance_time.restype = ctypes.c_int
        library.doodad_host_scenario_time.restype = ctypes.c_uint64
        library.doodad_host_deliver_provider.restype = ctypes.c_int

        library.doodad_host_ui_begin_document.argtypes = [
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int,
        ]
        library.doodad_host_ui_begin_document.restype = ctypes.c_void_p
        library.doodad_host_ui_add_stack.argtypes = [
            ctypes.c_void_p,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int,
        ]
        library.doodad_host_ui_add_stack.restype = ctypes.c_void_p
        library.doodad_host_ui_add_text.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p,
            ctypes.c_int,
        ]
        library.doodad_host_ui_add_text.restype = ctypes.c_void_p
        library.doodad_host_ui_add_button.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_int,
        ]
        library.doodad_host_ui_add_button.restype = ctypes.c_void_p
        library.doodad_host_ui_add_progress.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p,
            ctypes.c_int,
            ctypes.c_int,
        ]
        library.doodad_host_ui_add_progress.restype = ctypes.c_void_p

    def close(self) -> None:
        if getattr(self, "created", False):
            self.library.doodad_host_set_scene_operation_callback(
                SceneOperationCallback(),
                None,
            )
            self.library.doodad_host_destroy()
            NativeHost._shared_created = False
            self.created = False

    def __enter__(self) -> "NativeHost":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    def last_error(self) -> str:
        value = self.library.doodad_host_last_error()
        return value.decode("utf-8", errors="replace") if value else "unknown native error"

    def start_wasm(self, path: Path) -> None:
        if not self.library.doodad_host_start_wasm(str(path).encode()):
            raise DoodadError(self.last_error())

    def render_now(self) -> None:
        self.library.doodad_host_render_now()

    def show_catalog(self, story: int = 0) -> None:
        self.library.doodad_host_show_catalog(story)

    def show_appspec(self, canonical_cbor: bytes) -> None:
        payload = (ctypes.c_uint8 * len(canonical_cbor)).from_buffer_copy(
            canonical_cbor
        )
        if not self.library.doodad_host_show_appspec(
            payload, len(canonical_cbor)
        ):
            raise DoodadError(self.last_error())

    def click_first_action(self) -> None:
        if not self.library.doodad_host_click_first_action():
            raise DoodadError(self.last_error())

    def click_button(self, label: str) -> None:
        if not self.library.doodad_host_click_button(label.encode("utf-8")):
            raise DoodadError(self.last_error())

    def dispatch_semantic_action(
        self,
        node_id: str,
        action_id: str,
        event_kind: str,
        typed_value: bool | int | str | None = None,
    ) -> None:
        if event_kind not in self.EVENT_KINDS:
            raise ValueError(f"unsupported event kind: {event_kind}")
        value_kind = 0
        integer_value = 0
        boolean_value = 0
        text_value: bytes | None = None
        if isinstance(typed_value, bool):
            value_kind = 2
            boolean_value = int(typed_value)
        elif isinstance(typed_value, int):
            if not -(1 << 31) <= typed_value < (1 << 31):
                raise ValueError("semantic integer value is outside int32")
            value_kind = 1
            integer_value = typed_value
        elif isinstance(typed_value, str):
            value_kind = 3
            text_value = typed_value.encode("utf-8")
        elif typed_value is not None:
            raise TypeError("semantic typed_value must be bool, int, str, or None")
        if not self.library.doodad_host_dispatch_semantic_action(
            node_id.encode("utf-8"),
            action_id.encode("utf-8"),
            self.EVENT_KINDS[event_kind],
            value_kind,
            integer_value,
            boolean_value,
            text_value,
        ):
            raise DoodadError(self.last_error())

    def node_text(self, node_id: str) -> str:
        value = self.library.doodad_host_node_text(node_id.encode("utf-8"))
        if not value:
            raise DoodadError(f"node has no readable text: {node_id}")
        return value.decode("utf-8", errors="strict")

    def semantic_snapshot(self) -> str:
        length = self.library.doodad_host_semantic_snapshot(None, 0)
        if length == 0:
            raise DoodadError("no mounted semantic tree")
        output = ctypes.create_string_buffer(length + 1)
        written = self.library.doodad_host_semantic_snapshot(
            output, len(output)
        )
        if written != length:
            raise DoodadError(
                "semantic snapshot changed while it was being read"
            )
        return output.value.decode("utf-8", errors="strict")

    def scene_snapshot(self) -> str:
        length = self.library.doodad_host_scene_snapshot(None, 0)
        if length == 0:
            raise DoodadError("no mounted resolved scene")
        output = ctypes.create_string_buffer(length + 1)
        written = self.library.doodad_host_scene_snapshot(
            output, len(output)
        )
        if written != length:
            raise DoodadError(
                "resolved scene changed while it was being read"
            )
        return output.value.decode("utf-8", errors="strict")

    def node_evidence(
        self,
        capture_phase: dict[str, object] | None = None,
    ) -> dict[str, object]:
        snapshot = json.loads(self.scene_snapshot())
        length = self.library.doodad_host_node_layout_evidence(None, 0)
        if length == 0:
            raise DoodadError("no mounted node layout evidence")
        output = ctypes.create_string_buffer(length + 1)
        written = self.library.doodad_host_node_layout_evidence(
            output,
            len(output),
        )
        if written != length:
            raise DoodadError(
                "node layout changed while it was being read"
            )
        layout = json.loads(output.value.decode("utf-8", errors="strict"))
        evidence: dict[str, object] = {
            "schema_version": 1,
            "snapshot_sha256": document_sha256(snapshot),
            "capture_phase": capture_phase
            or {
                "id": "resting",
                "state": "resting",
                "animation_fraction_milli": 0,
            },
            "renderer": {
                "kind": "lvgl",
                "mode": "simulator",
                "version": "9.5.0",
                "build_sha256": hashlib.sha256(
                    self.library_path.read_bytes()
                ).hexdigest(),
            },
            "profile_id": "watch_square_240",
            "physical_width_px": self.WIDTH,
            "physical_height_px": self.HEIGHT,
            "nodes": layout["nodes"],
        }
        validate_node_evidence(evidence)
        return evidence

    def _receive_scene_operation(
        self,
        _context: int,
        scene_revision: int,
        route_generation: int,
        scenario_time_ms: int,
        cause_kind: int,
        cause_pointer: ctypes.POINTER(ctypes.c_uint8),
        cause_size: int,
        operation_kind: int,
        outcome: int,
        operation_pointer: ctypes.POINTER(ctypes.c_uint8),
        operation_size: int,
        snapshot_pointer: ctypes.POINTER(ctypes.c_char),
        snapshot_size: int,
    ) -> None:
        cause = (
            ctypes.string_at(cause_pointer, cause_size)
            if cause_pointer and cause_size
            else b""
        )
        operation = (
            ctypes.string_at(operation_pointer, operation_size)
            if operation_pointer and operation_size
            else b""
        )
        snapshot = (
            ctypes.string_at(snapshot_pointer, snapshot_size).decode(
                "utf-8",
                errors="strict",
            )
            if snapshot_pointer and snapshot_size
            else None
        )
        self._scene_operations.append(
            SceneOperation(
                scene_revision=int(scene_revision),
                route_generation=int(route_generation),
                scenario_time_ms=int(scenario_time_ms),
                cause_kind=int(cause_kind),
                cause=cause,
                operation_kind=int(operation_kind),
                outcome=int(outcome),
                operation=operation,
                snapshot_json=snapshot,
            )
        )

    def scene_operations(self, *, clear: bool = False) -> list[SceneOperation]:
        records = list(self._scene_operations)
        if clear:
            self._scene_operations.clear()
        return records

    def scene_revision(self) -> int:
        return int(self.library.doodad_host_scene_revision())

    def route_generation(self) -> int:
        return int(self.library.doodad_host_route_generation())

    def replay_mount(self, canonical_cbor: bytes, scenario_time_ms: int) -> None:
        payload = (ctypes.c_uint8 * len(canonical_cbor)).from_buffer_copy(
            canonical_cbor
        )
        if not self.library.doodad_host_replay_mount(
            payload,
            len(canonical_cbor),
            scenario_time_ms,
        ):
            raise DoodadError(self.last_error())

    def replay_command_batch(
        self,
        canonical_cbor: bytes,
        scenario_time_ms: int,
    ) -> None:
        payload = (ctypes.c_uint8 * len(canonical_cbor)).from_buffer_copy(
            canonical_cbor
        )
        if not self.library.doodad_host_replay_command_batch(
            payload,
            len(canonical_cbor),
            scenario_time_ms,
        ):
            raise DoodadError(self.last_error())

    def wasm_call_count(self) -> int:
        return int(self.library.doodad_host_wasm_call_count())

    def mounted_node_count(self) -> int:
        return int(self.library.doodad_host_mounted_node_count())

    def mounted_event_count(self) -> int:
        return int(self.library.doodad_host_mounted_event_count())

    def lvgl_object_count(self) -> int:
        return int(self.library.doodad_host_lvgl_object_count())

    def lvgl_max_depth(self) -> int:
        return int(self.library.doodad_host_lvgl_max_depth())

    def semantic_event_count(self) -> int:
        return int(self.library.doodad_host_semantic_event_count())

    def provider_request_count(self) -> int:
        return int(self.library.doodad_host_provider_request_count())

    def set_display_awake(self, awake: bool) -> None:
        self.library.doodad_host_set_display_awake(int(awake))

    def display_awake(self) -> bool:
        return bool(self.library.doodad_host_display_awake())

    def framebuffer_rgb565(self) -> bytes:
        self.render_now()
        pixel_count = self.library.doodad_host_framebuffer_pixels()
        expected = self.WIDTH * self.HEIGHT
        if pixel_count != expected:
            raise DoodadError(
                f"native framebuffer has {pixel_count} pixels; expected {expected}"
            )
        pixels = self.library.doodad_host_framebuffer()
        output = bytearray(pixel_count * 2)
        for index in range(pixel_count):
            value = int(pixels[index])
            output[index * 2] = value & 0xFF
            output[index * 2 + 1] = value >> 8
        return bytes(output)

    def advance_time(self, milliseconds: int) -> None:
        if milliseconds < 0:
            raise ValueError("milliseconds must be non-negative")
        if not self.library.doodad_host_advance_time(milliseconds):
            raise DoodadError(self.last_error())

    def scenario_time(self) -> int:
        return int(self.library.doodad_host_scenario_time())

    def deliver_provider(self) -> None:
        if not self.library.doodad_host_deliver_provider():
            raise DoodadError(self.last_error())

    def ui_begin_document(self, direction: int, align: int, gap: int) -> int:
        return int(self.library.doodad_host_ui_begin_document(direction, align, gap))

    def ui_add_stack(
        self, parent: int, direction: int, align: int, gap: int
    ) -> int:
        return int(
            self.library.doodad_host_ui_add_stack(parent, direction, align, gap)
        )

    def ui_add_text(self, parent: int, text: str, style: int) -> int:
        return int(
            self.library.doodad_host_ui_add_text(parent, text.encode("utf-8"), style)
        )

    def ui_add_button(
        self, parent: int, identifier: str, label: str, disabled: bool
    ) -> int:
        return int(
            self.library.doodad_host_ui_add_button(
                parent,
                identifier.encode("utf-8"),
                label.encode("utf-8"),
                int(disabled),
            )
        )

    def ui_add_progress(
        self, parent: int, label: str, value: int, maximum: int
    ) -> int:
        return int(
            self.library.doodad_host_ui_add_progress(
                parent, label.encode("utf-8"), value, maximum
            )
        )

    def write_bmp(self, path: Path) -> None:
        self.render_now()
        pixel_count = self.library.doodad_host_framebuffer_pixels()
        expected = self.WIDTH * self.HEIGHT
        if pixel_count != expected:
            raise DoodadError(
                f"native framebuffer has {pixel_count} pixels; expected {expected}"
            )
        pixels = self.library.doodad_host_framebuffer()
        row_bytes = self.WIDTH * 3
        image_bytes = row_bytes * self.HEIGHT
        header = struct.pack(
            "<2sIHHI",
            b"BM",
            54 + image_bytes,
            0,
            0,
            54,
        ) + struct.pack(
            "<IIIHHIIIIII",
            40,
            self.WIDTH,
            self.HEIGHT,
            1,
            24,
            0,
            image_bytes,
            2835,
            2835,
            0,
            0,
        )
        output = bytearray(header)
        for y in range(self.HEIGHT - 1, -1, -1):
            for x in range(self.WIDTH):
                value = int(pixels[y * self.WIDTH + x])
                red = ((value >> 11) & 0x1F) * 255 // 31
                green = ((value >> 5) & 0x3F) * 255 // 63
                blue = (value & 0x1F) * 255 // 31
                output.extend((blue, green, red))
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_suffix(path.suffix + ".tmp")
        temporary.write_bytes(output)
        temporary.replace(path)


def ensure_native_host(project_root: Path) -> Path:
    extension = ".dylib" if sys.platform == "darwin" else ".so"
    library = (
        project_root
        / "tools"
        / "native-host"
        / "build"
        / f"libdoodad_native_host{extension}"
    )
    inputs = [
        project_root / "firmware" / "dependencies.lock",
        project_root / "tools" / "native-host" / "CMakeLists.txt",
        project_root / "tools" / "native-host" / "lv_conf.h",
        project_root / "tools" / "native-host" / "include" / "doodad_native_host.h",
        project_root / "tools" / "native-host" / "src" / "doodad_native_host.c",
        project_root / "ui" / "doodad_lvgl_ui.c",
        project_root / "ui" / "doodad_lvgl_ui.h",
    ]
    inputs.extend(
        path
        for path in (project_root / "components" / "m3e_lvgl").rglob("*")
        if path.is_file() and path.suffix in {".cpp", ".hpp", ".h"}
    )
    if library.is_file() and all(
        not path.is_file() or path.stat().st_mtime_ns <= library.stat().st_mtime_ns
        for path in inputs
    ):
        return library
    result = subprocess.run(
        [str(project_root / "scripts" / "build-native-host.sh")],
        cwd=project_root,
        check=False,
    )
    if result.returncode != 0 or not library.is_file():
        raise DoodadError("native WAMR/LVGL host build failed")
    return library
