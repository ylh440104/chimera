package dev.chimera.app

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.EOFException
import java.io.IOException
import java.net.HttpURLConnection
import java.net.InetAddress
import java.net.ServerSocket
import java.net.URL
import java.security.MessageDigest
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class TransferBodyTest {
    @get:Rule val temporary = TemporaryFolder()

    @Test fun largeHttpResponseGoesToDisk() {
        val size = 33L * 1024 * 1024 + 1
        val pool = Executors.newSingleThreadExecutor()
        try {
            ServerSocket(0, 1, InetAddress.getByName("127.0.0.1")).use { server ->
                val sent = pool.submit {
                    server.accept().use { socket ->
                        socket.soTimeout = 15000
                        val reader = socket.getInputStream().bufferedReader(Charsets.US_ASCII)
                        while (!reader.readLine().isNullOrEmpty()) { }
                        val output = socket.getOutputStream()
                        output.write("HTTP/1.1 200 OK\r\nContent-Length: $size\r\nConnection: close\r\n\r\n".toByteArray(Charsets.US_ASCII))
                        val block = ByteArray(65536) { 'Z'.code.toByte() }
                        repeat(528) { output.write(block) }
                        output.write('!'.code)
                    }
                }
                val conn = URL("http://127.0.0.1:${server.localPort}/sample").openConnection() as HttpURLConnection
                conn.connectTimeout = 15000
                conn.readTimeout = 15000
                val file = temporary.newFile()
                try {
                    assertEquals(200, conn.responseCode)
                    conn.inputStream.use { input ->
                        file.outputStream().use { output ->
                            val body = TransferBody.receive(input, output, conn.contentLengthLong, { true }) { done, total ->
                                assertEquals(size, total)
                                assertTrue(done in 0..size)
                                true
                            }
                            assertEquals(0, body.size)
                        }
                    }
                } finally { conn.disconnect() }
                sent.get(15, TimeUnit.SECONDS)
                assertEquals(size, file.length())
                val digest = MessageDigest.getInstance("SHA-1")
                file.inputStream().use { input ->
                    val block = ByteArray(65536)
                    while (true) {
                        val count = input.read(block)
                        if (count < 0) break
                        digest.update(block, 0, count)
                    }
                }
                assertEquals("c73d1086a940fe6a14f909211904932ff9c74e6e", digest.digest().joinToString("") { "%02x".format(it) })
            }
        } finally { pool.shutdownNow() }
    }

    @Test fun truncationCancellationAndDataTaskLimit() {
        val content = byteArrayOf(3, 0, -4, 1)
        val ok = { true }
        val accept = { _: Long, _: Long -> true }
        assertArrayEquals(content, TransferBody.receive(content.inputStream(), null, -1, ok, accept))
        assertThrows(EOFException::class.java) {
            TransferBody.receive(content.inputStream(), ByteArrayOutputStream(), 5, ok, accept)
        }
        assertThrows(IOException::class.java) {
            TransferBody.receive(ByteArrayInputStream(content), null, 33L * 1024 * 1024, ok, accept)
        }
        val out = ByteArrayOutputStream()
        assertThrows(IOException::class.java) { TransferBody.receive(content.inputStream(), out, 4, ok) { _, _ -> false } }
        assertEquals(0, out.size())
    }
}
