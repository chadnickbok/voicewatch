#!/usr/bin/env python3

"""Small dependency-free inspector for the imports and exports this slice uses."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path


class WasmFormatError(ValueError):
    pass


@dataclass
class Reader:
    data: bytes
    offset: int = 0

    def byte(self) -> int:
        if self.offset >= len(self.data):
            raise WasmFormatError("unexpected end of file")
        value = self.data[self.offset]
        self.offset += 1
        return value

    def bytes(self, length: int) -> bytes:
        end = self.offset + length
        if end > len(self.data):
            raise WasmFormatError("unexpected end of file")
        value = self.data[self.offset:end]
        self.offset = end
        return value

    def uleb(self) -> int:
        result = 0
        shift = 0
        while True:
            value = self.byte()
            result |= (value & 0x7F) << shift
            if value & 0x80 == 0:
                return result
            shift += 7
            if shift > 63:
                raise WasmFormatError("invalid LEB128 integer")

    def name(self) -> str:
        return self.bytes(self.uleb()).decode("utf-8")


KIND_NAMES = {
    0: "function",
    1: "table",
    2: "memory",
    3: "global",
    4: "tag",
}

VALUE_TYPE_NAMES = {
    0x7F: "i32",
    0x7E: "i64",
    0x7D: "f32",
    0x7C: "f64",
}


def read_value_types(reader: Reader) -> tuple[str, ...]:
    values = []
    for _ in range(reader.uleb()):
        value = reader.byte()
        if value not in VALUE_TYPE_NAMES:
            raise WasmFormatError(f"unsupported value type 0x{value:02x}")
        values.append(VALUE_TYPE_NAMES[value])
    return tuple(values)


def skip_limits(reader: Reader) -> None:
    flags = reader.uleb()
    reader.uleb()
    if flags & 1:
        reader.uleb()


def skip_import_type(reader: Reader, kind: int) -> None:
    if kind == 0:
        reader.uleb()
    elif kind == 1:
        reader.byte()
        skip_limits(reader)
    elif kind == 2:
        skip_limits(reader)
    elif kind == 3:
        reader.bytes(2)
    elif kind == 4:
        reader.byte()
        reader.uleb()
    else:
        raise WasmFormatError(f"unknown import kind {kind}")


def read_limits(reader: Reader) -> tuple[int, int | None]:
    flags = reader.uleb()
    minimum = reader.uleb()
    maximum = reader.uleb() if flags & 1 else None
    return minimum, maximum


def inspect(
    path: Path,
) -> tuple[
    list[tuple[str, str, str]],
    list[tuple[str, str]],
    list[tuple[int, int | None]],
    dict[str, tuple[tuple[str, ...], tuple[str, ...]]],
]:
    reader = Reader(path.read_bytes())
    if reader.bytes(4) != b"\0asm" or reader.bytes(4) != b"\x01\0\0\0":
        raise WasmFormatError("not a WebAssembly 1.0 module")

    imports: list[tuple[str, str, str]] = []
    exports: list[tuple[str, str]] = []
    memories: list[tuple[int, int | None]] = []
    function_types: list[tuple[tuple[str, ...], tuple[str, ...]]] = []
    imported_function_type_indices: list[int] = []
    defined_function_type_indices: list[int] = []
    exported_function_indices: dict[str, int] = {}
    while reader.offset < len(reader.data):
        section_id = reader.byte()
        section = Reader(reader.bytes(reader.uleb()))
        if section_id == 1:
            for _ in range(section.uleb()):
                if section.byte() != 0x60:
                    raise WasmFormatError("unsupported function type")
                function_types.append(
                    (read_value_types(section), read_value_types(section))
                )
        elif section_id == 2:
            for _ in range(section.uleb()):
                module = section.name()
                name = section.name()
                kind = section.byte()
                imports.append((module, name, KIND_NAMES.get(kind, str(kind))))
                if kind == 0:
                    imported_function_type_indices.append(section.uleb())
                else:
                    skip_import_type(section, kind)
        elif section_id == 3:
            defined_function_type_indices = [
                section.uleb() for _ in range(section.uleb())
            ]
        elif section_id == 5:
            for _ in range(section.uleb()):
                memories.append(read_limits(section))
        elif section_id == 7:
            for _ in range(section.uleb()):
                name = section.name()
                kind = section.byte()
                index = section.uleb()
                exports.append((name, KIND_NAMES.get(kind, str(kind))))
                if kind == 0:
                    exported_function_indices[name] = index
    all_function_type_indices = (
        imported_function_type_indices + defined_function_type_indices
    )
    signatures = {}
    for name, function_index in exported_function_indices.items():
        if function_index >= len(all_function_type_indices):
            raise WasmFormatError("function export index outside function table")
        type_index = all_function_type_indices[function_index]
        if type_index >= len(function_types):
            raise WasmFormatError("function type index outside type section")
        signatures[name] = function_types[type_index]
    return imports, exports, memories, signatures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("wasm", type=Path)
    parser.add_argument("--verify-hello", action="store_true")
    arguments = parser.parse_args()

    imports, exports, memories, signatures = inspect(arguments.wasm)
    for module, name, kind in imports:
        print(f"import {module}.{name} ({kind})")
    for name, kind in exports:
        print(f"export {name} ({kind})")
        if kind == "function":
            params, results = signatures[name]
            print(
                f"  signature ({', '.join(params)})"
                f" -> ({', '.join(results)})"
            )
    for minimum, maximum in memories:
        print(f"memory minimum={minimum} page(s), maximum={maximum} page(s)")

    if arguments.verify_hello:
        expected_imports = [("doodad", "ui_mount", "function")]
        required_exports = {
            ("app_start", "function"),
            ("handle_event", "function"),
            ("memory", "memory"),
        }
        if imports != expected_imports:
            raise SystemExit(f"unexpected guest imports: {imports!r}")
        if not required_exports.issubset(set(exports)):
            raise SystemExit(f"missing guest exports: {required_exports - set(exports)!r}")
        if memories != [(1, 2)]:
            raise SystemExit(f"unexpected guest memory limits: {memories!r}")
        expected_signatures = {
            "app_start": ((), ()),
            "handle_event": (("i32", "i32"), ("i64",)),
        }
        for name, expected in expected_signatures.items():
            if signatures.get(name) != expected:
                raise SystemExit(
                    f"unexpected {name} signature: "
                    f"{signatures.get(name)!r}"
                )
        print("guest ABI verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
