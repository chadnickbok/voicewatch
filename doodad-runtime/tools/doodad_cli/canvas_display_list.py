from __future__ import annotations

import re

from .contract import DoodadError


MAX_CANVAS_EDGE = 192
MAX_DISPLAY_LIST_LENGTH = 128
MAX_PALETTE_LENGTH = 64
MAX_PALETTE_COLORS = 8
MAX_TILE_COUNT = 64

_PALETTE = re.compile(r"^[0-9a-f]{6}(?:,[0-9a-f]{6}){0,7}$")


def validate_canvas(
    display_list: object,
    palette: object,
    width: object,
    height: object,
    path: str,
) -> None:
    if (
        not isinstance(width, int)
        or isinstance(width, bool)
        or not 1 <= width <= MAX_CANVAS_EDGE
    ):
        raise DoodadError(f"{path}.width must be in 1..{MAX_CANVAS_EDGE}")
    if (
        not isinstance(height, int)
        or isinstance(height, bool)
        or not 1 <= height <= MAX_CANVAS_EDGE
    ):
        raise DoodadError(f"{path}.height must be in 1..{MAX_CANVAS_EDGE}")
    if (
        not isinstance(palette, str)
        or not 6 <= len(palette) <= MAX_PALETTE_LENGTH
        or _PALETTE.fullmatch(palette) is None
    ):
        raise DoodadError(
            f"{path}.palette must contain 1..{MAX_PALETTE_COLORS} "
            "lowercase RGB888 colors"
        )
    if (
        not isinstance(display_list, str)
        or not 1 <= len(display_list) <= MAX_DISPLAY_LIST_LENGTH
    ):
        raise DoodadError(
            f"{path}.display_list must contain "
            f"1..{MAX_DISPLAY_LIST_LENGTH} characters"
        )

    palette_size = palette.count(",") + 1
    commands = display_list.split("|")
    if not commands or commands[0] != "v1" or len(commands) == 1:
        raise DoodadError(f"{path}.display_list must begin with v1")
    if not commands[1].startswith("C"):
        raise DoodadError(f"{path}.display_list must clear before drawing")
    if len(commands) > 33:
        raise DoodadError(f"{path}.display_list has too many commands")

    for index, command in enumerate(commands[1:]):
        command_path = f"{path}.display_list command {index}"
        if not command:
            raise DoodadError(f"{command_path} is empty")
        opcode = command[0]
        arguments = command[1:].split(",")
        if opcode == "C":
            _expect(arguments, 1, command_path)
            _palette_index(arguments[0], palette_size, command_path)
        elif opcode == "R":
            _expect(arguments, 6, command_path)
            _palette_index(arguments[0], palette_size, command_path)
            x, y, rect_width, rect_height, radius = [
                _number(value, command_path) for value in arguments[1:]
            ]
            if (
                rect_width == 0
                or rect_height == 0
                or x + rect_width > width
                or y + rect_height > height
                or radius > min(rect_width, rect_height) // 2
            ):
                raise DoodadError(f"{command_path} is outside the canvas")
        elif opcode == "O":
            _expect(arguments, 4, command_path)
            _palette_index(arguments[0], palette_size, command_path)
            center_x, center_y, radius = [
                _number(value, command_path) for value in arguments[1:]
            ]
            if (
                radius == 0
                or center_x < radius
                or center_y < radius
                or center_x + radius > width
                or center_y + radius > height
            ):
                raise DoodadError(f"{command_path} is outside the canvas")
        elif opcode == "L":
            _expect(arguments, 6, command_path)
            _palette_index(arguments[0], palette_size, command_path)
            x1, y1, x2, y2, stroke = [
                _number(value, command_path) for value in arguments[1:]
            ]
            if (
                x1 >= width
                or x2 >= width
                or y1 >= height
                or y2 >= height
                or not 1 <= stroke <= 16
            ):
                raise DoodadError(f"{command_path} is outside the canvas")
        elif opcode == "T":
            _expect(arguments, 8, command_path)
            inset, x, y, cell_width, cell_height, columns, rows = [
                _number(value, command_path) for value in arguments[:7]
            ]
            cells = arguments[7]
            cell_count = columns * rows
            if (
                cell_width == 0
                or cell_height == 0
                or columns == 0
                or rows == 0
                or cell_count > MAX_TILE_COUNT
                or len(cells) != cell_count
                or inset * 2 >= min(cell_width, cell_height)
                or x + cell_width * columns > width
                or y + cell_height * rows > height
                or any(
                    character < "0"
                    or character > "7"
                    or int(character) >= palette_size
                    for character in cells
                )
            ):
                raise DoodadError(f"{command_path} has an invalid tile map")
        else:
            raise DoodadError(f"{command_path} uses unsupported opcode {opcode!r}")


def _expect(arguments: list[str], count: int, path: str) -> None:
    if len(arguments) != count:
        raise DoodadError(f"{path} has the wrong number of arguments")


def _number(value: str, path: str) -> int:
    if not value or not value.isascii() or not value.isdecimal():
        raise DoodadError(f"{path} contains a non-decimal argument")
    number = int(value)
    if number > MAX_CANVAS_EDGE:
        raise DoodadError(f"{path} contains an out-of-range argument")
    return number


def _palette_index(value: str, palette_size: int, path: str) -> int:
    number = _number(value, path)
    if number >= palette_size:
        raise DoodadError(f"{path} uses a missing palette color")
    return number
