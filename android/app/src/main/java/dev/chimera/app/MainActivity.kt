package dev.chimera.app

import android.net.Uri
import android.os.Bundle
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import java.io.File
import java.io.FileOutputStream
import java.util.zip.ZipFile

class MainActivity : AppCompatActivity() {

    private lateinit var logView: TextView

    private val picker =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
            if (uri != null) start(uri)
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val root = LinearLayout(this)
        root.orientation = LinearLayout.VERTICAL
        root.setPadding(48, 96, 48, 48)

        val button = Button(this)
        button.text = "导入 IPA"
        button.setOnClickListener { picker.launch(arrayOf("*/*")) }

        logView = TextView(this)
        logView.textSize = 11f
        logView.setTextIsSelectable(true)

        val scroll = ScrollView(this)
        scroll.addView(logView)

        root.addView(button)
        root.addView(
            scroll,
            LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f)
        )
        setContentView(root)

        val pending = File(filesDir, "pending.ipa")
        if (pending.isFile) {
            log("自动运行 pending.ipa")
            Thread { runFile(pending) }.start()
        }
    }

    private fun log(text: String) {
        runOnUiThread {
            logView.append(text)
            logView.append("\n")
        }
    }

    private fun start(uri: Uri) {
        Thread {
            try {
                val cached = File(cacheDir, "input.ipa")
                log("读取 " + uri)
                val afd = contentResolver.openAssetFileDescriptor(uri, "r")
                if (afd == null) {
                    log("无法打开该文件")
                    return@Thread
                }
                val declared = afd.length
                log("声明长度 " + declared)
                afd.use { d ->
                    FileOutputStream(cached).use { out ->
                        d.createInputStream().use { input ->
                            val buf = ByteArray(1 shl 16)
                            var total = 0L
                            var mark = 0L
                            while (true) {
                                val n = input.read(buf)
                                if (n <= 0) break
                                out.write(buf, 0, n)
                                total += n
                                if (total - mark >= (8 shl 20)) {
                                    mark = total
                                    log("已读 " + total)
                                }
                            }
                            log("读完 " + total)
                        }
                    }
                }
                runFile(cached)
            } catch (t: Throwable) {
                log("失败 " + t)
            }
        }.start()
    }

    private fun runFile(ipa: File) {
        try {
            log("解包 " + ipa.name + " " + ipa.length() + " 字节")
            val work = File(cacheDir, "payload")
            work.deleteRecursively()
            work.mkdirs()

            var count = 0
            ZipFile(ipa).use { zip ->
                val entries = zip.entries()
                while (entries.hasMoreElements()) {
                    val entry = entries.nextElement()
                    val name = entry.name
                    if (name.contains("..")) continue
                    val out = File(work, name)
                    if (entry.isDirectory) {
                        out.mkdirs()
                        continue
                    }
                    out.parentFile?.mkdirs()
                    zip.getInputStream(entry).use { input ->
                        out.outputStream().use { output -> input.copyTo(output) }
                    }
                    count++
                }
            }
            log("解出 " + count + " 个文件")

            val app = File(work, "Payload").listFiles()
                ?.firstOrNull { it.isDirectory && it.name.endsWith(".app") }
            if (app == null) {
                log("没有 Payload/*.app")
                return
            }
            log("bundle " + app.name)

            val exeName = readExecutable(File(app, "Info.plist"))
            if (exeName == null) {
                log("Info.plist 里没有 CFBundleExecutable")
                return
            }
            val exe = File(app, exeName)
            if (!exe.isFile) {
                log("找不到 " + exeName)
                return
            }
            log("exe " + exeName + " " + exe.length() + " 字节")

            val magic = ByteArray(4)
            exe.inputStream().use { it.read(magic) }
            log("magic " + magic.joinToString(" ") { String.format("%02x", it) })

            val rc = Native.run(exe.absolutePath)
            log("返回 " + rc)
        } catch (t: Throwable) {
            log("失败 " + t)
            t.stackTrace.take(4).forEach { log("  at " + it) }
        }
    }

    private fun readExecutable(plist: File): String? {
        if (!plist.isFile) return null
        val text = try {
            plist.readText()
        } catch (t: Throwable) {
            return null
        }
        val match = Regex("<key>CFBundleExecutable</key>\\s*<string>([^<]+)</string>").find(text)
        return match?.groupValues?.get(1)
    }
}