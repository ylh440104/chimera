package dev.chimera.app

import android.graphics.Canvas
import android.graphics.Paint
import android.text.StaticLayout
import kotlin.math.max

internal class FrameCommand(
    private val x: Float,
    private val y: Float,
    private val width: Float,
    private val height: Float,
    private val left: Float,
    private val top: Float,
    private val right: Float,
    private val bottom: Float,
    private val paint: Paint?,
    private val layout: StaticLayout?,
    private val rounded: Boolean
) {
    fun replay(canvas: Canvas) {
        val saved = canvas.save()
        try {
            if (!canvas.clipRect(left, top, right, bottom)) return
            if (layout != null) {
                canvas.clipRect(x, y, x + width, y + height)
                canvas.translate(x, y + max(0f, (height - layout.height) / 2))
                layout.draw(canvas)
            } else if (paint != null) {
                if (rounded) {
                    val radius = minOf(width, height) / 2
                    canvas.drawRoundRect(x, y, x + width, y + height, radius, radius, paint)
                } else canvas.drawRect(x, y, x + width, y + height, paint)
            }
        } finally {
            canvas.restoreToCount(saved)
        }
    }
}

class GuestFrame internal constructor(
    val width: Int,
    val height: Int,
    commands: List<FrameCommand>,
    texts: List<String>,
    val rectangles: Int
) {
    private val commands = commands.toList()
    val texts: List<String> = texts.toList()

    fun replay(canvas: Canvas) {
        commands.forEach { it.replay(canvas) }
    }
}
