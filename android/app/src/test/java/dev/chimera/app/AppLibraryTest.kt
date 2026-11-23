package dev.chimera.app

import com.dd.plist.BinaryPropertyListWriter
import com.dd.plist.NSDictionary
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
class AppLibraryTest {
    @get:Rule val temp = TemporaryFolder()
    private val files by lazy { temp.newFolder("files") }
    private val cache by lazy { temp.newFolder("cache") }
    private fun library() = AppLibrary(files, cache)

    private fun plist(id: String, version: String, binary: Boolean): ByteArray {
        val dict = NSDictionary()
        dict.put("CFBundleIdentifier", id)
        dict.put("CFBundleDisplayName", "同名应用")
        dict.put("CFBundleExecutable", "Sample")
        dict.put("CFBundleShortVersionString", version)
        val nested = NSDictionary()
        nested.put("CFBundleIdentifier", "nested.must.not.be.selected")
        dict.put("Nested", nested)
        return if (binary) ByteArrayOutputStream().also { BinaryPropertyListWriter.write(it, dict) }.toByteArray()
            else dict.toXMLPropertyList().toByteArray()
    }

    private fun ipa(id: String = "dev.test.one", version: String = "1.0", binary: Boolean = false,
                    missingExe: Boolean = false, traversal: Boolean = false): ByteArray {
        val buffer = ByteArrayOutputStream()
        ZipOutputStream(buffer).use { zip ->
            fun entry(path: String, data: ByteArray) {
                zip.putNextEntry(ZipEntry(path))
                zip.write(data)
                zip.closeEntry()
            }
            entry("Payload/Sample.app/Info.plist", plist(id, version, binary))
            if (!missingExe) entry("Payload/Sample.app/Sample", byteArrayOf(0xcf.toByte(), 0xfa.toByte(), 0xed.toByte(), 0xfe.toByte()) + ByteArray(28))
            entry("Payload/Sample.app/version.txt", version.toByteArray())
            if (traversal) entry("../escape.txt", byteArrayOf(1))
        }
        return buffer.toByteArray()
    }

    private fun save(app: InstalledApp, path: String, value: String) = File(app.sandbox, path).apply {
        parentFile!!.mkdirs()
        writeText(value)
    }

    @Test fun multipleAppsSurviveNewLibraryAndCacheRemoval() {
        val store = library()
        val first = store.install(ipa().inputStream())
        val second = store.install(ipa("dev.test.two", binary = true).inputStream())
        assertNotEquals(first.key, second.key)
        assertEquals(first.name, second.name)
        save(first, "mobile/Documents/world.dat", "first world")
        save(second, "mobile/Documents/world.dat", "second world")
        save(first, "Library/Preferences/preferences.plist", "first settings")
        cache.deleteRecursively()
        val reopened = library().snapshot()
        assertEquals(2, reopened.apps.size)
        assertTrue(reopened.problems.isEmpty())
        for (app in reopened.apps) {
            assertTrue(app.executable.isFile)
            assertTrue(File(app.directory, "source.ipa").length() > 0)
            val expected = if (app.identifier == first.identifier) "first world" else "second world"
            assertEquals(expected, File(app.sandbox, "mobile/Documents/world.dat").readText())
        }
        assertEquals("first settings", File(library().find(first.key)!!.sandbox, "Library/Preferences/preferences.plist").readText())
        assertTrue(File(second.bundle, "Info.plist").readText().startsWith("<?xml"))
    }

    @Test fun updateKeepsDataAndFailedImportsKeepPreviousVersion() {
        val first = library().install(ipa().inputStream())
        val other = library().install(ipa("dev.test.other").inputStream())
        save(first, "mobile/Documents/world.dat", "saved")
        val updated = library().install(ipa(version = "2.0").inputStream())
        assertEquals(first.sandbox, updated.sandbox)
        assertFalse(first.directory.exists())
        assertEquals("saved", File(updated.sandbox, "mobile/Documents/world.dat").readText())
        assertEquals("2.0", File(updated.bundle, "version.txt").readText())
        assertThrows(IllegalArgumentException::class.java) { library().install(ipa(version = "3.0", missingExe = true).inputStream()) }
        assertThrows(IllegalArgumentException::class.java) { library().install(ipa(traversal = true).inputStream()) }
        assertFalse(File(files, "escape.txt").exists())
        assertThrows(IOException::class.java) {
            library().install(object : InputStream() { override fun read(): Int = throw IOException("interrupted source") })
        }
        assertThrows(IllegalStateException::class.java) { library().install(ipa().inputStream(), { false }) }
        assertEquals("2.0", library().find(first.key)!!.version)
        assertTrue(library().find(other.key)!!.executable.isFile)
        assertTrue(File(files, "library").listFiles().orEmpty().none { it.name.startsWith(".import-") })
    }

    @Test fun runningAppCannotBeReplacedOrRemoved() {
        val app = library().install(ipa().inputStream())
        library().acquire(app.key).use {
            assertThrows(IllegalStateException::class.java) { library().install(ipa(version = "2.0").inputStream()) }
            assertThrows(IllegalStateException::class.java) { library().remove(app.key, true) }
            assertEquals("1.0", library().find(app.key)!!.version)
        }
        assertEquals("2.0", library().install(ipa(version = "2.0").inputStream()).version)
    }

    @Test fun legacyImportKeepsExactSandboxAndSurvivesRestart() {
        val key = AppLibrary.keyFor("dev.test.one")
        val original = File(files, "applications/$key/mobile/Documents/world.dat")
        original.parentFile!!.mkdirs()
        original.writeText("legacy saved data")
        val source = File(cache, "staging/input.ipa")
        source.parentFile!!.mkdirs()
        source.writeBytes(ipa())
        val expanded = File(cache, "app/Payload/Sample.app")
        expanded.mkdirs()
        File(expanded, "legacy-resource").writeText("cache")
        val migrated = library().migrateLegacy()!!
        assertEquals(key, migrated.key)
        assertEquals("legacy saved data", original.readText())
        assertEquals(original, File(migrated.sandbox, "mobile/Documents/world.dat"))
        assertFalse(source.exists())
        assertFalse(expanded.exists())
        assertNull(library().migrateLegacy())
        assertTrue(library().find(key)!!.executable.isFile)
    }

    @Test fun failedMigrationKeepsLegacyAndMigrationDoesNotDowngradeInstalledApp() {
        val source = File(cache, "staging/input.ipa")
        source.parentFile!!.mkdirs()
        source.writeBytes(ipa(missingExe = true))
        assertThrows(IllegalArgumentException::class.java) { library().migrateLegacy() }
        assertTrue(source.isFile)
        val installed = library().install(ipa(version = "2.0").inputStream())
        source.writeBytes(ipa(version = "1.0"))
        val migrated = library().migrateLegacy()!!
        assertEquals(installed.directory, migrated.directory)
        assertEquals("2.0", migrated.version)
    }

    @Test fun removalKeepsDataByDefaultAndExplicitEraseIsIsolated() {
        val first = library().install(ipa().inputStream())
        val other = library().install(ipa("dev.test.other").inputStream())
        save(first, "mobile/Documents/world.dat", "keep me")
        save(other, "mobile/Documents/world.dat", "other")
        library().remove(first.key, false)
        assertNull(library().find(first.key))
        assertTrue(first.sandbox.isDirectory)
        val reinstalled = library().install(ipa().inputStream())
        assertEquals("keep me", File(reinstalled.sandbox, "mobile/Documents/world.dat").readText())
        library().remove(first.key, true)
        assertFalse(first.sandbox.exists())
        assertEquals("other", File(library().find(other.key)!!.sandbox, "mobile/Documents/world.dat").readText())
    }
}
