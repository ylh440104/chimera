package dev.chimera.app

import org.json.JSONObject
import java.io.ByteArrayOutputStream
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
    val cancelled: Boolean
)

class NetworkResult(val request: NetworkRequest, val status: Int, val body: ByteArray, val headers: String, val error: String)

object GuestNetwork {
    private val executor = Executors.newFixedThreadPool(4)

    fun send(request: NetworkRequest, complete: (NetworkResult) -> Unit) {
        executor.execute {
            var connection: HttpURLConnection? = null
            val result = try {
                check(!request.cancelled) { "Request cancelled" }
                val url = URL(request.url)
                require(url.protocol == "https" || url.protocol == "http") { "Unsupported URL scheme" }
                connection = url.openConnection() as HttpURLConnection
                val conn = requireNotNull(connection)
                conn.connectTimeout = request.timeout.coerceIn(1000, 300000)
                conn.readTimeout = request.timeout.coerceIn(1000, 300000)
                conn.requestMethod = request.method
                conn.instanceFollowRedirects = true
                val headers = JSONObject(request.headers)
                headers.keys().forEach { key -> conn.setRequestProperty(key, headers.getString(key)) }
                if (request.body.isNotEmpty()) {
                    conn.doOutput = true
                    conn.setFixedLengthStreamingMode(request.body.size)
                    conn.outputStream.use { it.write(request.body) }
                }
                val status = conn.responseCode
                val responseHeaders = JSONObject()
                conn.headerFields.forEach { (key, value) -> if (key != null) responseHeaders.put(key, value.joinToString(", ")) }
                val stream = if (status >= 400) conn.errorStream else conn.inputStream
                val output = ByteArrayOutputStream()
                stream?.use { input ->
                    val buffer = ByteArray(32768)
                    while (true) {
                        val count = input.read(buffer)
                        if (count < 0) break
                        check(output.size() + count <= 32 * 1024 * 1024) { "Response exceeds data task limit" }
                        output.write(buffer, 0, count)
                    }
                }
                NetworkResult(request, status, output.toByteArray(), responseHeaders.toString(), "")
            } catch (e: Exception) {
                NetworkResult(request, 0, ByteArray(0), "{}", e.message ?: e.javaClass.simpleName)
            } finally {
                connection?.disconnect()
            }
            Native.executor.execute { complete(result) }
        }
    }
}