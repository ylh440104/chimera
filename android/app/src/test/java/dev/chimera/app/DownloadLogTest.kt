package dev.chimera.app

import android.content.ContentProvider
import android.content.ContentValues
import android.content.pm.ProviderInfo
import android.database.Cursor
import android.net.Uri
import android.os.Environment
import android.provider.MediaStore
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config
import org.robolectric.shadows.ShadowContentResolver
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
class DownloadLogTest {
    @Test fun snapshotContainsOnlyThisSessionAndRetainsLastReportOnTimeout() {
        val first = GuestSessionLog(64)
        first.identify("应用甲", "1.2", "test.one")
        first.append("第一条")
        repeat(5) { first.append("操作 $it") }
        first.updateReport("已加载")
        assertTrue(first.snapshot().contains("应用甲 1.2 (test.one)"))
        assertTrue(first.snapshot().contains("仅保留最近"))
        assertTrue(first.snapshot().contains("操作 4"))
        assertTrue(first.snapshot().contains("已加载"))
        assertTrue(first.snapshot("当前客机状态").contains("当前客机状态"))
        assertFalse(first.snapshot().contains("第一条"))
        val second = GuestSessionLog()
        second.append("打开失败")
        assertTrue(second.snapshot().contains("打开失败"))
        assertFalse(second.snapshot().contains("test.one"))
        assertFalse(second.snapshot().contains("已加载"))
    }

    private class DownloadProvider : ContentProvider() {
        val created = mutableListOf<ContentValues>()
        val streams = mutableMapOf<Uri, ByteArrayOutputStream>()
        val published = mutableListOf<Uri>()
        val removed = mutableListOf<Uri>()
        var failPublish = false
        override fun onCreate() = true
        override fun getType(uri: Uri) = "text/plain"
        override fun query(uri: Uri, projection: Array<out String>?, selection: String?, selectionArgs: Array<out String>?, sortOrder: String?): Cursor? = null
        override fun insert(uri: Uri, values: ContentValues?): Uri {
            assertEquals(MediaStore.Downloads.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY), uri)
            created.add(ContentValues(requireNotNull(values)))
            val target = Uri.withAppendedPath(uri, created.size.toString())
            val stream = ByteArrayOutputStream()
            streams[target] = stream
            shadowOf(requireNotNull(context).contentResolver).registerOutputStream(target, stream)
            return target
        }
        override fun update(uri: Uri, values: ContentValues?, selection: String?, selectionArgs: Array<out String>?): Int {
            assertTrue(streams.getValue(uri).size() > 0)
            assertEquals(0, values!!.getAsInteger(MediaStore.MediaColumns.IS_PENDING).toInt())
            if (failPublish) return 0
            published.add(uri)
            return 1
        }
        override fun delete(uri: Uri, selection: String?, selectionArgs: Array<out String>?): Int {
            removed.add(uri)
            return 1
        }
    }

    @Test fun publicDownloadsPublishesCompleteUtf8AndRollsBackFailure() {
        val context = RuntimeEnvironment.getApplication()
        val provider = DownloadProvider()
        provider.attachInfo(context, ProviderInfo().apply { authority = "media" })
        ShadowContentResolver.registerProviderInternal("media", provider)
        val first = DownloadLog.save(context, "客机甲\n启动失败\n")
        val second = DownloadLog.save(context, "客机乙\n运行中\n")
        assertNotEquals(first, second)
        assertEquals(listOf(first, second), provider.created.map { "Download/" + it.getAsString(MediaStore.MediaColumns.DISPLAY_NAME) })
        for (row in provider.created) {
            assertEquals("Download/", row.getAsString(MediaStore.MediaColumns.RELATIVE_PATH))
            assertEquals("text/plain", row.getAsString(MediaStore.MediaColumns.MIME_TYPE))
            assertEquals(1, row.getAsInteger(MediaStore.MediaColumns.IS_PENDING).toInt())
        }
        assertEquals(listOf("客机甲\n启动失败\n", "客机乙\n运行中\n"), provider.published.map { provider.streams.getValue(it).toString("UTF-8") })
        provider.failPublish = true
        assertThrows(IOException::class.java) { DownloadLog.save(context, "写入测试") }
        assertEquals(2, provider.published.size)
        val downloads = MediaStore.Downloads.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
        assertEquals(listOf(Uri.withAppendedPath(downloads, "3")), provider.removed)
    }

    @Test @Config(sdk = [28]) fun legacyExportsToPublicDownloadRootWithoutOverwriting() {
        val context = RuntimeEnvironment.getApplication()
        val dir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS)
        val paths = listOf("旧设备甲", "旧设备乙").map { contents ->
            val relative = DownloadLog.save(context, contents)
            assertTrue(relative.matches(Regex("Download/Chimera-log-[^/]+\\.txt")))
            File(dir, relative.substringAfter('/'))
        }
        assertNotEquals(paths[0], paths[1])
        assertEquals(listOf("旧设备甲", "旧设备乙"), paths.map { it.readText(Charsets.UTF_8) })
    }
}