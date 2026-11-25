package dev.chimera.app

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
class NativeUTTypeTest {
    @Test
    fun resolvesFileTypesThroughDeviceJni() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val directory = File(instrumentation.targetContext.cacheDir, "uttype-${UUID.randomUUID()}")
        assertTrue(directory.mkdirs())
        try {
            val executable = File(directory, "darwin_uttype")
            instrumentation.context.assets.open("darwin_uttype").use { source ->
                executable.outputStream().use { source.copyTo(it) }
            }
            val sandbox = File(directory, "root")
            assertTrue(sandbox.mkdirs())
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
            directory.deleteRecursively()
        }
    }
}
