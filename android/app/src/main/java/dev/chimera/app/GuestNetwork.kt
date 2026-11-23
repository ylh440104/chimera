package dev.chimera.app

import org.json.JSONObject
import android.os.ParcelFileDescriptor
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.Executors

class NetworkRequest(
    val token: Long,
    val url: String,
    val method: String,
    val headers: String,
    val body: ByteArray,
    val timeout: Int,
    val cancelled: Boolean,
    val download: Boolean,
    val downloadFd: Int
)

class NetworkResult(val request: NetworkRequest, val status: Int, val body: ByteArray, val headers: String, val error: String)

object GuestNetwork {
    private val executor = Executors.newFixedThreadPool(4)

    fun send(request: NetworkRequest, active: () -> Boolean, progress: (Long, Long) -> Boolean,
             complete: (NetworkResult) -> Unit) {
        executor.execute {
            var status = 0
            var fields = "{}"
            val result = try {
                val output = if (request.downloadFd < 0) null else
                    ParcelFileDescriptor.AutoCloseOutputStream(ParcelFileDescriptor.adoptFd(request.downloadFd))
                val bytes = output.use { file ->
                    check(active() && !request.cancelled) { "Request cancelled" }
                    check(!request.download || file != null) { "Download temporary file unavailable" }
                    val url = URL(request.url)
                    require(url.protocol == "https" || url.protocol == "http") { "Unsupported URL scheme" }
                    val conn = url.openConnection() as HttpURLConnection
                    try {
                        conn.connectTimeout = request.timeout.coerceIn(1000, 300000)
                        conn.readTimeout = conn.connectTimeout
                        conn.requestMethod = request.method
                        conn.instanceFollowRedirects = true
                        val headers = JSONObject(request.headers)
                        headers.keys().forEach { key -> conn.setRequestProperty(key, headers.getString(key)) }
                        if (request.body.isNotEmpty()) {
                            conn.doOutput = true
                            conn.setFixedLengthStreamingMode(request.body.size)
                            conn.outputStream.use { it.write(request.body) }
                        }
                        status = conn.responseCode
                        val responseHeaders = JSONObject()
                        conn.headerFields.forEach { (key, value) -> if (key != null) responseHeaders.put(key, value.joinToString(", ")) }
                        fields = responseHeaders.toString()
                        val empty = request.method.equals("HEAD", true) || status == 204 || status == 304
                        val expected = if (empty) 0L else conn.contentLengthLong
                        val stream = if (status >= 400) conn.errorStream else conn.inputStream
                        stream.use { TransferBody.receive(it, file, expected, active, progress) }
                    } finally { conn.disconnect() }
                }
                NetworkResult(request, status, bytes, fields, "")
            } catch (e: Exception) {
                NetworkResult(request, status, ByteArray(0), fields, e.message ?: e.javaClass.simpleName)
            }
            Native.executor.execute { complete(result) }
        }
    }
}