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
class NativeSecurityTest {
    @Test
    fun validatesCertificatesAndRejectsInvalidTrustThroughJni() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val directory = File(instrumentation.targetContext.cacheDir, "security-${UUID.randomUUID()}")
        assertTrue(directory.mkdirs())
        try {
            val executable = File(directory, "darwin_security")
            instrumentation.context.assets.open("darwin_security").use { source ->
                executable.outputStream().use { source.copyTo(it) }
            }
            val sandbox = File(directory, "root")
            val certificates = File(sandbox, "mobile/security")
            assertTrue(certificates.mkdirs())
            for (name in instrumentation.context.assets.list("security").orEmpty()) {
                instrumentation.context.assets.open("security/$name").use { source ->
                    File(certificates, name).outputStream().use { source.copyTo(it) }
                }
            }
            Native.executor.submit {
                repeat(3) {
                    try {
                        val loaded = Native.load(executable.absolutePath, sandbox.absolutePath, 720, 1280)
                        assertTrue(loaded, Native.ready())
                        assertEquals(Native.report(), "entry returned 0", Native.run())
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