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
]:
    reader = Reader(path.read_bytes())
    if reader.bytes(4) != b"\0asm" or reader.bytes(4) != b"\x01\0\0\0":
        raise WasmFormatError("not a WebAssembly 1.0 module")

    imports: list[tuple[str, str, str]] = []
    exports: list[tuple[str, str]] = []
    memories: list[tuple[int, int | None]] = []
    while reader.offset < len(reader.data):
        section_id = reader.byte()
        section = Reader(reader.bytes(reader.uleb()))
        if section_id == 2:
            for _ in range(section.uleb()):
                module = section.name()
                name = section.name()
                kind = section.byte()
                imports.append((module, name, KIND_NAMES.get(kind, str(kind))))
                skip_import_type(section, kind)
        elif section_id == 5:
            for _ in range(section.uleb()):
                memories.append(read_limits(section))
        elif section_id == 7:
            for _ in range(section.uleb()):
                name = section.name()
                kind = section.byte()
                section.uleb()
                exports.append((name, KIND_NAMES.get(kind, str(kind))))
    return imports, exports, memories


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("wasm", type=Path)
    parser.add_argument("--verify-hello", action="store_true")
    arguments = parser.parse_args()

    imports, exports, memories = inspect(arguments.wasm)
    for module, name, kind in imports:
        print(f"import {module}.{name} ({kind})")
    for name, kind in exports:
        print(f"export {name} ({kind})")
    for minimum, maximum in memories:
        print(f"memory minimum={minimum} page(s), maximum={maximum} page(s)")

    if arguments.verify_hello:
        expected_imports = [("doodad", "display_text", "function")]
        required_exports = {("app_start", "function"), ("memory", "memory")}
        if imports != expected_imports:
            raise SystemExit(f"unexpected guest imports: {imports!r}")
        if not required_exports.issubset(set(exports)):
            raise SystemExit(f"missing guest exports: {required_exports - set(exports)!r}")
        if memories != [(1, 2)]:
            raise SystemExit(f"unexpected guest memory limits: {memories!r}")
        print("guest ABI verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
