package dev.chimera.app

import org.junit.Assert.assertEquals
import org.junit.Test
import java.util.ArrayDeque
import java.util.concurrent.Executor

class GuestGestureTest {
    @Test fun moveBeforeUpAndTapAfterDrag() {
        val events = mutableListOf<PanEvent>()
        var taps = 0
        val gesture = GuestGesture({ phase, x, y -> events.add(PanEvent(1L, phase, x, y)) }, { _, _, _, _ -> taps++ })
        gesture.begin(10f, 80f, 5f)
        gesture.sample(10f, 78f, false, true)
        assertEquals(0, events.size)
        gesture.sample(10f, 60f, false, true)
        assertEquals(listOf(PanPhase.BEGIN, PanPhase.MOVE), events.map { it.phase })
        gesture.sample(8f, -3f, true, false)
        assertEquals(PanEvent(1L, PanPhase.END, 8f, -3f), events.last())
        assertEquals(0, taps)
        gesture.begin(20f, 10f, 5f)
        gesture.sample(21f, 10f, true, true)
        assertEquals(1, taps)
    }

    @Test fun cancelAndQueueBoundaries() {
        val pending = ArrayDeque<Runnable>()
        val received = mutableListOf<PanEvent>()
        val queue = PanQueue(Executor { pending.add(it) }) { received.add(it) }
        var epoch = 1L
        val gesture = GuestGesture({ phase, x, y -> queue.offer(PanEvent(epoch, phase, x, y)) }, { _, _, _, _ -> error("pan became a tap") })
        gesture.begin(10f, 100f, 2f)
        for (y in 90 downTo 0) gesture.sample(10f, y.toFloat(), false, true)
        assertEquals(2, pending.size)
        gesture.cancel()
        gesture.sample(11f, 1f, true, true)
        epoch++
        gesture.begin(50f, 50f, 2f)
        gesture.sample(70f, 48f, true, true)
        while (pending.isNotEmpty()) pending.removeFirst().run()
        assertEquals(listOf(PanPhase.BEGIN, PanPhase.MOVE, PanPhase.CANCEL, PanPhase.BEGIN, PanPhase.END), received.map { it.phase })
        assertEquals(PanEvent(1, PanPhase.MOVE, 10f, 0f), received[1])
        assertEquals(PanEvent(2, PanPhase.END, 70f, 48f), received.last())
    }
}
