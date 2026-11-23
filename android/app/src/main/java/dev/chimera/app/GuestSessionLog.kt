package dev.chimera.app

import java.time.ZoneId
import java.time.ZonedDateTime
import java.time.format.DateTimeFormatter
import java.util.ArrayDeque

internal class GuestSessionLog(private val limit: Int = 128 * 1024) {
    private val started = ZonedDateTime.now(ZoneId.of("Asia/Shanghai"))
    private val clock = DateTimeFormatter.ofPattern("HH:mm:ss.SSS")
    private val events = ArrayDeque<String>()
    private var size = 0
    private var truncated = false
    private var app = "打开应用中"
    private var report = "尚无原生报告"

    @Synchronized fun identify(name: String, version: String, identifier: String) {
        app = "$name $version ($identifier)"
    }

    @Synchronized fun append(message: String) {
        val time = clock.format(ZonedDateTime.now(started.zone))
        val entry = "[$time] $message\n"
        if (entry.length > limit) truncated = true
        val retained = entry.takeLast(limit)
        while (events.isNotEmpty() && size + retained.length > limit) {
            size -= events.removeFirst().length
            truncated = true
        }
        events.addLast(retained)
        size += retained.length
    }

    @Synchronized fun updateReport(value: String) {
        report = value.takeLast(32 * 1024)
    }

    @Synchronized fun snapshot(latestReport: String? = null): String = buildString {
        appendLine("Chimera 运行日志")
        appendLine("应用：$app")
        appendLine("会话开始：${started.format(DateTimeFormatter.ISO_OFFSET_DATE_TIME)}")
        appendLine()
        if (truncated) appendLine("（仅保留最近的运行记录）")
        events.forEach { append(it) }
        appendLine()
        appendLine("原生运行报告")
        appendLine(latestReport ?: report)
    }
}