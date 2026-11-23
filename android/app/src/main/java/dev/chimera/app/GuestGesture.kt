package dev.chimera.app

import java.util.concurrent.Executor
import kotlin.math.hypot

internal enum class PanPhase(val code: Int) { BEGIN(0), MOVE(1), END(2), CANCEL(3) }

internal class GuestGesture(
    private val pan: (PanPhase, Float, Float) -> Unit,
    private val tap: (Float, Float, Float, Float) -> Unit
) {
    private var active = false
    private var dragging = false
    private var downX = 0f
    private var downY = 0f
    private var slop = 0f

    fun begin(x: Float, y: Float, threshold: Float) {
        cancel()
        downX = x
        downY = y
        slop = threshold
        active = true
    }

    fun sample(x: Float, y: Float, up: Boolean, inside: Boolean) {
        if (!active) return
        if (!dragging && hypot(x - downX, y - downY) > slop) {
            dragging = true
            pan(PanPhase.BEGIN, downX, downY)
        }
        val wasDragging = dragging
        if (up) {
            active = false
            dragging = false
        }
        if (wasDragging) pan(if (up) PanPhase.END else PanPhase.MOVE, x, y)
        else if (up && inside) tap(downX, downY, x, y)
    }

    fun cancel() {
        val notify = active && dragging
        active = false
        dragging = false
        if (notify) pan(PanPhase.CANCEL, downX, downY)
    }
}

internal data class PanEvent(val epoch: Long, val phase: PanPhase, val x: Float, val y: Float)

internal class PanQueue(private val executor: Executor, private val deliver: (PanEvent) -> Unit) {
    private class Slot(var event: PanEvent)
    private var tail: Slot? = null

    @Synchronized fun offer(event: PanEvent) {
        val pending = tail
        if (event.phase == PanPhase.MOVE && pending != null && pending.event.epoch == event.epoch) {
            pending.event = event
            return
        }
        val slot = Slot(event)
        tail = if (event.phase == PanPhase.MOVE) slot else null
        executor.execute {
            val sample = synchronized(this) {
                if (tail === slot) tail = null
                slot.event
            }
            deliver(sample)
        }
    }
}