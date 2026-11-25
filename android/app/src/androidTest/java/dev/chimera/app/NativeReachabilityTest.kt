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
class NativeReachabilityTest {
    @Test
    fun monitorsRoutesAndDeliversCallbacksThroughJni() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val directory = File(instrumentation.targetContext.cacheDir, "reachability-${UUID.randomUUID()}")
        assertTrue(directory.mkdirs())
        try {
            val executable = File(directory, "darwin_reachability")
            instrumentation.context.assets.open("darwin_reachability").use { source ->
                executable.outputStream().use { source.copyTo(it) }
            }
            val sandbox = File(directory, "root")
            assertTrue(sandbox.mkdirs())
            Native.executor.submit {
                repeat(3) {
                    try {
                        val loaded = Native.load(executable.absolutePath, sandbox.absolutePath, 720, 1280)
                        assertTrue(loaded, Native.ready())
                        assertEquals(Native.report(), "entry returned 0", Native.run())
                        var result = "entry returned 199"
                        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(8)
                        while (result == "entry returned 199" && System.nanoTime() < deadline) {
                            assertTrue(Native.report(), Native.pump() >= 0)
                            result = Native.run()
                            if (result == "entry returned 199") Thread.sleep(10)
                        }
                        assertEquals(Native.report(), "entry returned 0", result)
                        repeat(4) { assertTrue(Native.report(), Native.pump() >= 0) }
                        assertEquals(Native.report(), "entry returned 0", Native.run())
                    } finally {
                        Native.release()
                    }
                }
            }.get(40, TimeUnit.SECONDS)
        } finally {
            directory.deleteRecursively()
        }
    }
}
