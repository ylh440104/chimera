package dev.chimera.app

import android.app.Activity
import android.view.MotionEvent
import android.view.View
import android.widget.ImageButton
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import org.robolectric.shadows.ShadowPopupMenu

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
class GuestScreenTest {
    @Test fun fullSizeImageAndTwoIndependentMenuActions() {
        val controller = Robolectric.buildActivity(Activity::class.java).setup()
        try {
            val actions = mutableListOf<String>()
            val screen = GuestScreen(controller.get(), { actions.add("library") }, { actions.add("export") })
            controller.get().setContentView(screen)
            screen.showStatus(null)
            fun spec(value: Int) = View.MeasureSpec.makeMeasureSpec(value, View.MeasureSpec.EXACTLY)
            screen.measure(spec(1080), spec(2400))
            screen.layout(0, 0, 1080, 2400)
            val visible = (0 until screen.childCount).map(screen::getChildAt).filter { it.visibility == View.VISIBLE }
            assertEquals(2, visible.size)
            assertEquals(1080, screen.image.width)
            assertEquals(2400, screen.image.height)
            assertEquals(0, screen.image.top)
            val more = visible.filterIsInstance<ImageButton>().single()
            assertTrue(more.left > screen.width / 2 && more.bottom < screen.height / 2)
            for (index in 1 downTo 0) {
                more.performClick()
                val popup = requireNotNull(ShadowPopupMenu.getLatestPopupMenu())
                val menu = popup.menu
                assertEquals(listOf("应用库", "导出日志"), (0 until menu.size()).map { menu.getItem(it).title.toString() })
                assertTrue(menu.performIdentifierAction(menu.getItem(index).itemId, 0))
                popup.dismiss()
            }
            assertEquals(listOf("export", "library"), actions)
        } finally { controller.pause().stop().destroy() }
    }

    @Test fun portraitAndLandscapeEdgesMapToTheGuest() {
        val image = GuestImageView(RuntimeEnvironment.getApplication())
        assertEquals(GuestViewport(720, 1600), GuestViewport.fromPixels(1080, 2400))
        assertEquals(GuestViewport(1600, 720), GuestViewport.fromPixels(2400, 1080))
        assertEquals(2048, GuestViewport.fromPixels(800, 3840).height)
        for ((w, h) in listOf(1080 to 2400, 2400 to 1080)) {
            val size = GuestViewport.fromPixels(w, h)
            image.layout(0, 0, w, h)
            image.submitFrame(FramePainter(size.width, size.height).finish())
            image.acceptInput = true
            var taps = 0
            image.onGuestTap = { _, up ->
                assertEquals(size.width - 2 / 3f, up.x, 0.001f)
                assertEquals(size.height - 2 / 3f, up.y, 0.001f)
                taps++
            }
            for (action in listOf(MotionEvent.ACTION_DOWN, MotionEvent.ACTION_UP)) {
                val event = MotionEvent.obtain(100L, 101L, action, w - 1f, h - 1f, 0)
                try { assertTrue(image.dispatchTouchEvent(event)) } finally { event.recycle() }
            }
            assertEquals(1, taps)
        }
    }
}