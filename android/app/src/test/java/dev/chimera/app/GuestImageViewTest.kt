package dev.chimera.app

import android.graphics.Bitmap
import android.graphics.Canvas
import android.view.MotionEvent
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
class GuestImageViewTest {
    @Test fun dragSurvivesFrameReplacement() {
        val view = GuestImageView(RuntimeEnvironment.getApplication())
        view.layout(0, 0, 400, 640)
        fun frame() = FramePainter(720, 1280).finish()
        view.submitFrame(frame())
        view.acceptInput = true
        val pans = mutableListOf<PanEvent>()
        var taps = 0
        view.onGuestPan = { phase, x, y -> pans.add(PanEvent(1, phase, x, y)) }
        view.onGuestTap = { _, _ -> taps++ }
        var t = 100L
        fun touch(action: Int, x: Float, y: Float): Boolean {
            val event = MotionEvent.obtain(100L, t++, action, x, y, 0)
            return try { view.dispatchTouchEvent(event) } finally { event.recycle() }
        }
        assertFalse(touch(MotionEvent.ACTION_DOWN, 5f, 400f))
        assertTrue(touch(MotionEvent.ACTION_DOWN, 120f, 400f))
        assertTrue(touch(MotionEvent.ACTION_MOVE, 120f, 340f))
        assertEquals(listOf(PanEvent(1, PanPhase.BEGIN, 200f, 800f), PanEvent(1, PanPhase.MOVE, 200f, 680f)), pans)
        view.submitFrame(frame())
        assertTrue(touch(MotionEvent.ACTION_MOVE, 120f, 300f))
        assertEquals(PanEvent(1, PanPhase.MOVE, 200f, 600f), pans.last())
        assertTrue(touch(MotionEvent.ACTION_UP, 120f, 299f))
        assertEquals(PanEvent(1, PanPhase.END, 200f, 598f), pans.last())
        assertEquals(0, taps)
        touch(MotionEvent.ACTION_DOWN, 120f, 299f)
        view.submitFrame(frame())
        touch(MotionEvent.ACTION_UP, 120f, 299f)
        assertEquals(1, taps)
        touch(MotionEvent.ACTION_DOWN, 120f, 299f)
        touch(MotionEvent.ACTION_MOVE, 120f, 100f)
        view.submitFrame(null)
        assertEquals(PanPhase.CANCEL, pans.last().phase)
        assertEquals(1, taps)
    }

    @Test @GraphicsMode(GraphicsMode.Mode.NATIVE) fun vectorIsScaledTintedAndClipped() {
        val painter = FramePainter(80, 80)
        val blue = 0xff0a84ff.toInt()
        painter.draw(3, "M10 1H14V10H23V14H14V23H10V14H1V10H10Z".toByteArray(),
            doubleArrayOf(10.0, 12.0, 48.0, 48.0, 10.0, 12.0, 44.0, 60.0, 0.0), blue, 0, 0)
        val bitmap = Bitmap.createBitmap(80, 80, Bitmap.Config.ARGB_8888)
        painter.finish().replay(Canvas(bitmap))
        assertEquals(blue, bitmap.getPixel(34, 35))
        assertEquals(0, bitmap.getPixel(15, 16))
        assertEquals(0, bitmap.getPixel(47, 35))
    }
}