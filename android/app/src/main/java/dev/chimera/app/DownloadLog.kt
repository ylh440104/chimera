package dev.chimera.app

import android.content.ContentResolver
import android.content.ContentValues
import android.content.Context
import android.media.MediaScannerConnection
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import androidx.annotation.RequiresApi
import java.io.File
import java.io.IOException
import java.time.ZoneId
import java.time.ZonedDateTime
import java.time.format.DateTimeFormatter
import java.util.Locale
import java.util.UUID
import java.util.concurrent.Executors

internal object DownloadLog {
    val executor = Executors.newSingleThreadExecutor()
    private val stamp = DateTimeFormatter.ofPattern("yyyyMMdd-HHmmss", Locale.ROOT)

    @Suppress("DEPRECATION")
    fun save(context: Context, text: String): String {
        val now = ZonedDateTime.now(ZoneId.of("Asia/Shanghai"))
        val name = "Chimera-log-${now.format(stamp)}-${UUID.randomUUID().toString().take(8)}.txt"
        if (Build.VERSION.SDK_INT >= 29) {
            publish(context.contentResolver, name, text)
        } else {
            val dir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS)
            if (!dir.isDirectory && !dir.mkdirs()) throw IOException("无法打开 Download 文件夹")
            val file = File(dir, name)
            if (!file.createNewFile()) throw IOException("日志文件已存在，请重试")
            try {
                file.outputStream().bufferedWriter(Charsets.UTF_8).use { it.write(text) }
            } catch (e: Exception) {
                file.delete()
                throw e
            }
            MediaScannerConnection.scanFile(context, arrayOf(file.absolutePath), arrayOf("text/plain"), null)
        }
        return "${Environment.DIRECTORY_DOWNLOADS}/$name"
    }

    @RequiresApi(29)
    private fun publish(resolver: ContentResolver, name: String, text: String) {
        val values = ContentValues().apply {
            put(MediaStore.MediaColumns.DISPLAY_NAME, name)
            put(MediaStore.MediaColumns.MIME_TYPE, "text/plain")
            put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS + "/")
            put(MediaStore.MediaColumns.IS_PENDING, 1)
        }
        val collection = MediaStore.Downloads.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
        val uri = resolver.insert(collection, values)
            ?: throw IOException("无法在 Download 文件夹创建日志")
        try {
            val stream = resolver.openOutputStream(uri) ?: throw IOException("无法打开日志文件")
            stream.bufferedWriter(Charsets.UTF_8).use { it.write(text) }
            values.clear()
            values.put(MediaStore.MediaColumns.IS_PENDING, 0)
            if (resolver.update(uri, values, null, null) != 1) throw IOException("无法发布日志")
        } catch (e: Exception) {
            runCatching { resolver.delete(uri, null, null) }
            throw e
        }
    }
}
