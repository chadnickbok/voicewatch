from __future__ import annotations

import ctypes
import struct
import subprocess
import sys
from pathlib import Path

from .contract import DoodadError


class NativeHost:
    WIDTH = 240
    HEIGHT = 240

    def __init__(self, project_root: Path):
        self.project_root = project_root
        self.library_path = ensure_native_host(project_root)
        self.library = ctypes.CDLL(str(self.library_path))
        self._configure()
        if not self.library.doodad_host_create():
            raise DoodadError(self.last_error())
        self.created = True

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
            self.library.doodad_host_destroy()
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
