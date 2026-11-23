package dev.chimera.app

import java.io.ByteArrayOutputStream
import java.io.EOFException
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream

internal object TransferBody {
    private const val DATA_LIMIT = 32L * 1024 * 1024

    fun receive(input: InputStream?, file: OutputStream?, expected: Long, active: () -> Boolean,
                progress: (Long, Long) -> Boolean): ByteArray {
        val memory = if (file == null) ByteArrayOutputStream() else null
        val out = file ?: requireNotNull(memory)
        if (memory != null && expected > DATA_LIMIT) throw IOException("Response exceeds data task limit")
        val buffer = ByteArray(64 * 1024)
        var count = 0L
        var last = System.nanoTime()
        fun notifyProgress() {
            if (file != null && !progress(count, expected)) throw IOException("Request cancelled")
        }
        if (!active()) throw IOException("Request cancelled")
        notifyProgress()
        while (input != null) {
            if (!active()) throw IOException("Request cancelled")
            val n = input.read(buffer)
            if (n < 0) break
            if (memory != null && count + n > DATA_LIMIT) throw IOException("Response exceeds data task limit")
            out.write(buffer, 0, n)
            count += n
            val now = System.nanoTime()
            if (now - last >= 200_000_000L) { notifyProgress(); last = now }
        }
        if (expected >= 0 && count != expected) throw EOFException("Expected $expected bytes, received $count")
        if (!active()) throw IOException("Request cancelled")
        out.flush()
        return memory?.toByteArray() ?: ByteArray(0)
    }
}
