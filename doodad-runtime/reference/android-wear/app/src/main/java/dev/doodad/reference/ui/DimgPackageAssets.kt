package dev.doodad.reference.ui

import android.content.res.AssetManager
import android.graphics.Bitmap
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import java.security.MessageDigest

internal object DimgPackageAssets {
    private const val HeaderBytes = 12
    private const val Rgb565LittleEndian = 1

    fun decode(
        assets: AssetManager,
        sha256: String,
    ): ImageBitmap? =
        runCatching {
            val payload =
                assets.open("$sha256.dimg").use { it.readBytes() }
            check(payload.sha256() == sha256)
            check(payload.size >= HeaderBytes)
            check(payload.copyOfRange(0, 4).contentEquals("DIMG".encodeToByteArray()))
            val width = payload.littleEndianU16(4)
            val height = payload.littleEndianU16(6)
            check(width in 1..512 && height in 1..512)
            check(payload[8].unsigned() == Rgb565LittleEndian)
            check(payload[9].unsigned() == 0)
            check(payload.littleEndianU16(10) == 0)
            check(payload.size == HeaderBytes + width * height * 2)

            val pixels = IntArray(width * height)
            var source = HeaderBytes
            pixels.indices.forEach { index ->
                val rgb565 =
                    payload[source].unsigned() or
                        (payload[source + 1].unsigned() shl 8)
                source += 2
                val red = ((rgb565 shr 11) and 0x1f) * 255 / 31
                val green = ((rgb565 shr 5) and 0x3f) * 255 / 63
                val blue = (rgb565 and 0x1f) * 255 / 31
                pixels[index] =
                    (0xff shl 24) or
                        (red shl 16) or
                        (green shl 8) or
                        blue
            }
            Bitmap
                .createBitmap(width, height, Bitmap.Config.ARGB_8888)
                .apply {
                    setPixels(pixels, 0, width, 0, 0, width, height)
                }
                .asImageBitmap()
        }.getOrNull()

    private fun Byte.unsigned(): Int = toInt() and 0xff

    private fun ByteArray.littleEndianU16(offset: Int): Int =
        this[offset].unsigned() or (this[offset + 1].unsigned() shl 8)

    private fun ByteArray.sha256(): String =
        MessageDigest
            .getInstance("SHA-256")
            .digest(this)
            .joinToString("") { "%02x".format(it) }
}
