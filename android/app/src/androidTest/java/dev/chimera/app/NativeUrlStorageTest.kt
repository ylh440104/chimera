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
class NativeUrlStorageTest {
    @Test
    fun persistsAndReopensUrlCacheThroughJni() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val directory = File(instrumentation.targetContext.cacheDir, "url-storage-${UUID.randomUUID()}")
        assertTrue(directory.mkdirs())
        try {
            val executable = File(directory, "darwin_url_storage")
            instrumentation.context.assets.open("darwin_url_storage").use { source ->
                executable.outputStream().use { source.copyTo(it) }
            }
            val sandbox = File(directory, "root")
            val certificates = File(sandbox, "mobile/security")
            assertTrue(certificates.mkdirs())
            instrumentation.context.assets.open("security/system-root.der").use { source ->
                File(certificates, "system-root.der").outputStream().use { source.copyTo(it) }
            }
            Native.executor.submit {
                repeat(3) {
                    try {
                        val loaded = Native.load(executable.absolutePath, sandbox.absolutePath, 720, 1280)
                        assertTrue(loaded, Native.ready())
                        assertEquals(Native.report(), "entry returned 0", Native.run())
                        assertEquals(Native.report(), "entry returned 0", Native.run())
                    } finally {
                        Native.release()
                    }
                }
            }.get(60, TimeUnit.SECONDS)
        } finally {
            directory.deleteRecursively()
        }
    }
}
