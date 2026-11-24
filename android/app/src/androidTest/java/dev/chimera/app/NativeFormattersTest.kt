package dev.chimera.app

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.util.Locale
import java.util.TimeZone
import java.util.UUID
import java.util.concurrent.TimeUnit
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class NativeFormattersTest {
    @Test
    fun formatsThroughDeviceJniAndSystemIcu() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val directory = File(instrumentation.targetContext.cacheDir, "formatters-${UUID.randomUUID()}")
        assertTrue(directory.mkdirs())
        val executable = File(directory, "darwin_formatters")
        instrumentation.context.assets.open("darwin_formatters").use { source ->
            executable.outputStream().use { source.copyTo(it) }
        }
        val sandbox = File(directory, "root")
        assertTrue(File(sandbox, "mobile/version").mkdirs())
        val locale = Locale.getDefault()
        val zone = TimeZone.getDefault()
        try {
            Locale.setDefault(Locale.US)
            TimeZone.setDefault(TimeZone.getTimeZone("Asia/Shanghai"))
            Native.executor.submit {
                repeat(3) {
                    try {
                        val loaded = Native.load(executable.absolutePath, sandbox.absolutePath, 720, 1280)
                        assertTrue(loaded, Native.ready())
                        val result = Native.run()
                        assertEquals(Native.report(), "entry returned 0", result)
                    } finally {
                        Native.release()
                    }
                }
            }.get(45, TimeUnit.SECONDS)
        } finally {
            Locale.setDefault(locale)
            TimeZone.setDefault(zone)
            directory.deleteRecursively()
        }
    }
}