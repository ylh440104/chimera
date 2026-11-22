package dev.chimera.app

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.text.Layout
import android.text.StaticLayout
import android.text.TextPaint
import kotlin.math.ceil
import kotlin.math.max

class FramePainter(width: Int, height: Int) {
    val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
    private val canvas = Canvas(bitmap)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val textPaint = TextPaint(Paint.ANTI_ALIAS_FLAG)
    val texts = mutableListOf<String>()
    var rectangles = 0
        private set

    fun draw(kind: Int, bytes: ByteArray?, geometry: DoubleArray, color: Int, alignment: Int, lines: Int) {
        require(geometry.size == 9 && geometry.all { it.isFinite() })
        val x = geometry[0].toFloat()
        val y = geometry[1].toFloat()
        val w = geometry[2].toFloat()
        val h = geometry[3].toFloat()
        if (w <= 0 || h <= 0) return
        val saved = canvas.save()
        try {
            if (!canvas.clipRect(geometry[4].toFloat(), geometry[5].toFloat(), geometry[6].toFloat(), geometry[7].toFloat())) return
            when (kind) {
                0 -> {
                    paint.color = color
                    canvas.drawRect(x, y, x + w, y + h, paint)
                    rectangles++
                }
                1 -> {
                    val text = bytes?.toString(Charsets.UTF_8) ?: return
                    if (text.isEmpty()) return
                    textPaint.color = color
                    textPaint.textSize = geometry[8].toFloat().coerceIn(1f, 1024f)
                    val align = when (alignment) {
                        1 -> Layout.Alignment.ALIGN_CENTER
                        2 -> Layout.Alignment.ALIGN_OPPOSITE
                        else -> Layout.Alignment.ALIGN_NORMAL
                    }
                    val layout = StaticLayout.Builder.obtain(text, 0, text.length, textPaint, ceil(w.toDouble()).toInt().coerceIn(1, 16384))
                        .setAlignment(align)
                        .setIncludePad(false)
                        .setMaxLines(if (lines > 0) lines else Int.MAX_VALUE)
                        .build()
                    canvas.clipRect(x, y, x + w, y + h)
                    canvas.translate(x, y + max(0f, (h - layout.height) / 2))
                    layout.draw(canvas)
                    texts.add(text)
                }
                else -> error("Unknown draw command: $kind")
            }
        } finally {
            canvas.restoreToCount(saved)
        }
    }
}
