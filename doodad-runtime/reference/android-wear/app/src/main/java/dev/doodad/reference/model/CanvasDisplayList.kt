package dev.doodad.reference.model

data class ParsedCanvasDisplayList(
    val palette: List<Int>,
    val commands: List<CanvasCommand>,
)

sealed interface CanvasCommand {
    data class Clear(
        val color: Int,
    ) : CanvasCommand

    data class RoundedRect(
        val color: Int,
        val x: Int,
        val y: Int,
        val width: Int,
        val height: Int,
        val radius: Int,
    ) : CanvasCommand

    data class Circle(
        val color: Int,
        val centerX: Int,
        val centerY: Int,
        val radius: Int,
    ) : CanvasCommand

    data class Line(
        val color: Int,
        val x1: Int,
        val y1: Int,
        val x2: Int,
        val y2: Int,
        val stroke: Int,
    ) : CanvasCommand

    data class TileMap(
        val inset: Int,
        val x: Int,
        val y: Int,
        val cellWidth: Int,
        val cellHeight: Int,
        val columns: Int,
        val rows: Int,
        val cells: List<Int>,
    ) : CanvasCommand
}

object CanvasDisplayListCodec {
    fun parse(
        displayList: String,
        palette: String,
        width: Int,
        height: Int,
    ): ParsedCanvasDisplayList {
        check(width in 1..192 && height in 1..192) {
            "Canvas dimensions must be in 1..192"
        }
        check(displayList.length in 1..128) {
            "Canvas Display List must contain 1..128 characters"
        }
        check(
            palette.length in 6..64 &&
                palette.matches(Regex("^[0-9a-f]{6}(,[0-9a-f]{6}){0,7}$")),
        ) {
            "Canvas palette must contain 1..8 lowercase RGB888 colors"
        }
        val colors =
            palette.split(",").map { encoded ->
                encoded.toInt(16)
            }
        val encodedCommands = displayList.split("|")
        check(
            encodedCommands.size in 2..33 &&
                encodedCommands.first() == "v1" &&
                encodedCommands[1].startsWith("C"),
        ) {
            "Canvas Display List must begin with v1 and clear"
        }
        var cleared = false
        val commands =
            encodedCommands.drop(1).mapIndexed { index, encoded ->
                check(encoded.isNotEmpty()) {
                    "Canvas command $index is empty"
                }
                val opcode = encoded.first()
                val arguments = encoded.drop(1).split(",")
                when (opcode) {
                    'C' -> {
                        check(!cleared && index == 0 && arguments.size == 1)
                        cleared = true
                        CanvasCommand.Clear(
                            colorIndex(arguments[0], colors.size),
                        )
                    }
                    'R' -> {
                        check(arguments.size == 6)
                        val values = arguments.map(::number)
                        val color = colorIndex(arguments[0], colors.size)
                        val x = values[1]
                        val y = values[2]
                        val rectWidth = values[3]
                        val rectHeight = values[4]
                        val radius = values[5]
                        check(
                            rectWidth > 0 &&
                                rectHeight > 0 &&
                                x + rectWidth <= width &&
                                y + rectHeight <= height &&
                                radius <= minOf(rectWidth, rectHeight) / 2,
                        )
                        CanvasCommand.RoundedRect(
                            color,
                            x,
                            y,
                            rectWidth,
                            rectHeight,
                            radius,
                        )
                    }
                    'O' -> {
                        check(arguments.size == 4)
                        val values = arguments.map(::number)
                        val color = colorIndex(arguments[0], colors.size)
                        val centerX = values[1]
                        val centerY = values[2]
                        val radius = values[3]
                        check(
                            radius > 0 &&
                                centerX >= radius &&
                                centerY >= radius &&
                                centerX + radius <= width &&
                                centerY + radius <= height,
                        )
                        CanvasCommand.Circle(
                            color,
                            centerX,
                            centerY,
                            radius,
                        )
                    }
                    'L' -> {
                        check(arguments.size == 6)
                        val values = arguments.map(::number)
                        val color = colorIndex(arguments[0], colors.size)
                        check(
                            values[1] < width &&
                                values[3] < width &&
                                values[2] < height &&
                                values[4] < height &&
                                values[5] in 1..16,
                        )
                        CanvasCommand.Line(
                            color,
                            values[1],
                            values[2],
                            values[3],
                            values[4],
                            values[5],
                        )
                    }
                    'T' -> {
                        check(arguments.size == 8)
                        val values = arguments.take(7).map(::number)
                        val inset = values[0]
                        val x = values[1]
                        val y = values[2]
                        val cellWidth = values[3]
                        val cellHeight = values[4]
                        val columns = values[5]
                        val rows = values[6]
                        val cellData = arguments[7]
                        val cellCount = columns * rows
                        check(
                            cellWidth > 0 &&
                                cellHeight > 0 &&
                                columns > 0 &&
                                rows > 0 &&
                                cellCount <= 64 &&
                                cellData.length == cellCount &&
                                inset * 2 < minOf(cellWidth, cellHeight) &&
                                x + cellWidth * columns <= width &&
                                y + cellHeight * rows <= height,
                        )
                        val cells =
                            cellData.map { character ->
                                colorIndex(character.toString(), colors.size)
                            }
                        CanvasCommand.TileMap(
                            inset,
                            x,
                            y,
                            cellWidth,
                            cellHeight,
                            columns,
                            rows,
                            cells,
                        )
                    }
                    else -> error("Unsupported Canvas opcode $opcode")
                }
            }
        return ParsedCanvasDisplayList(colors, commands)
    }

    private fun number(encoded: String): Int {
        check(encoded.isNotEmpty() && encoded.all { it in '0'..'9' })
        return encoded.toInt().also {
            check(it in 0..192)
        }
    }

    private fun colorIndex(encoded: String, count: Int): Int =
        number(encoded).also {
            check(it in 0 until count)
        }
}
