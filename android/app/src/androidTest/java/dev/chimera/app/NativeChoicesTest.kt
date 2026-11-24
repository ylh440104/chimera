package dev.chimera.app

import android.graphics.Bitmap
import android.graphics.Canvas
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.util.UUID
import java.util.concurrent.TimeUnit
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class NativeChoicesTest {
    private fun frame(): GuestFrame {
        val painter = FramePainter(400, 600)
        assertEquals(Native.report(), 1, Native.paint(painter, 400, 600))
        return painter.finish()
    }

    @Test
    fun paintsAndDispatchesChoicesThroughJni() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val directory = File(instrumentation.targetContext.cacheDir, "choices-${UUID.randomUUID()}")
        assertTrue(directory.mkdirs())
        val executable = File(directory, "darwin_choices")
        instrumentation.context.assets.open("darwin_choices").use { source ->
            executable.outputStream().use { source.copyTo(it) }
        }
        val sandbox = File(directory, "root")
        assertTrue(sandbox.mkdirs())
        try {
            Native.executor.submit {
                repeat(3) {
                    try {
                        val loaded = Native.load(executable.absolutePath, sandbox.absolutePath, 400, 600)
                        assertTrue(loaded, Native.ready())
                        assertEquals(Native.report(), "entry returned 0", Native.run())
                        val initial = frame()
                        assertTrue(initial.texts.toString(), initial.texts.containsAll(listOf("One", "Two", "Three", "P0-1", "Custom-1", "Header title", "Header detail", "Footer title", "ready")))
                        val bitmap = Bitmap.createBitmap(400, 600, Bitmap.Config.ARGB_8888)
                        try {
                            initial.replay(Canvas(bitmap))
                            assertEquals(0xff0a84ff.toInt(), bitmap.getPixel(70, 24))
                        } finally {
                            bitmap.recycle()
                        }
                        assertEquals(0, Native.tap(170.0, 40.0, 170.0, 40.0))
                        assertTrue(frame().texts.contains("ready"))
                        assertEquals(1, Native.tap(270.0, 40.0, 270.0, 40.0))
                        assertTrue(frame().texts.contains("segment:2:1"))
                        assertEquals(0, Native.tap(270.0, 40.0, 270.0, 40.0))
                        assertEquals(1, Native.tap(70.0, 220.0, 70.0, 220.0))
                        assertTrue(frame().texts.contains("picker:0:2:1"))
                        assertEquals(0, Native.pan(0, 70.0, 180.0))
                        assertEquals(1, Native.pan(1, 70.0, 100.0))
                        assertTrue(frame().texts.contains("picker:0:2:1"))
                        assertEquals(1, Native.pan(2, 70.0, 100.0))
                        assertTrue(frame().texts.contains("picker:0:4:2"))
                        assertEquals(1, Native.tap(200.0, 210.0, 200.0, 210.0))
                        assertTrue(frame().texts.contains("picker:1:2:3"))
                    } finally {
                        Native.release()
                    }
                }
            }.get(45, TimeUnit.SECONDS)
        } finally {
            directory.deleteRecursively()
        }
    }
}