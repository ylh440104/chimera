package dev.chimera.app

import android.graphics.Paint
import android.text.Layout
import android.text.StaticLayout
import android.text.TextPaint
import kotlin.math.ceil

class FramePainter(private val width: Int, private val height: Int) {
    private val commands = mutableListOf<FrameCommand>()
    private val texts = mutableListOf<String>()
    private var rectangles = 0
    private var finished = false
    private val paths = mutableMapOf<String, android.graphics.Path>()

    init {
        require(width in 1..2048 && height in 1..2048)
    }

    fun draw(kind: Int, bytes: ByteArray?, geometry: DoubleArray, color: Int, alignment: Int, lines: Int) {
        check(!finished)
        require(geometry.size == 9 && geometry.all { it.isFinite() && it.toFloat().isFinite() })
        val x = geometry[0].toFloat()
        val y = geometry[1].toFloat()
        val w = geometry[2].toFloat()
        val h = geometry[3].toFloat()
        if (w <= 0 || h <= 0 || geometry[4] >= geometry[6] || geometry[5] >= geometry[7]) return
        var fill: Paint? = null
        var layout: StaticLayout? = null
        var symbol: android.graphics.Path? = null
        when (kind) {
            3 -> {
                val data = bytes?.toString(Charsets.UTF_8) ?: return
                symbol = paths.getOrPut(data) {
                    requireNotNull(androidx.core.graphics.PathParser.createPathFromPathData(data)).apply {
                        fillType = android.graphics.Path.FillType.EVEN_ODD
                    }
                }
                fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { this.color = color }
            }
            0, 2 -> {
                fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { this.color = color }
                rectangles++
            }
            1 -> {
                val text = bytes?.toString(Charsets.UTF_8) ?: return
                if (text.isEmpty()) return
                val textPaint = TextPaint(Paint.ANTI_ALIAS_FLAG).apply {
                    this.color = color
                    textSize = geometry[8].toFloat().coerceIn(1f, 1024f)
                }
                val align = when (alignment) {
                    1 -> Layout.Alignment.ALIGN_CENTER
                    2 -> Layout.Alignment.ALIGN_OPPOSITE
                    else -> Layout.Alignment.ALIGN_NORMAL
                }
                layout = StaticLayout.Builder.obtain(text, 0, text.length, textPaint, ceil(w.toDouble()).toInt().coerceIn(1, 16384))
                    .setAlignment(align)
                    .setIncludePad(false)
                    .setMaxLines(if (lines > 0) lines else Int.MAX_VALUE)
                    .build()
                texts.add(text)
            }
            else -> error("Unknown draw command: $kind")
        }
        commands.add(FrameCommand(x, y, w, h, geometry[4].toFloat(), geometry[5].toFloat(),
            geometry[6].toFloat(), geometry[7].toFloat(), fill, layout, kind == 2, symbol))
    }

    fun finish(): GuestFrame {
        check(!finished)
        finished = true
        return GuestFrame(width, height, commands, texts, rectangles)
    }
}
