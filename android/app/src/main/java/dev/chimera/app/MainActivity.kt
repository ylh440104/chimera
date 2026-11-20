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
import java.util.zip.ZipFile

class MainActivity : AppCompatActivity() {

    private lateinit var logView: TextView
    private lateinit var openButton: Button
    private lateinit var runButton: Button
    private var job: Thread? = null

    private val picker =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
            if (uri != null) importAndLoad(uri)
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val root = LinearLayout(this)
        root.orientation = LinearLayout.VERTICAL
        root.setPadding(48, 96, 48, 48)

        openButton = Button(this)
        openButton.text = "导入 IPA"
        openButton.setOnClickListener { picker.launch(arrayOf("*/*")) }

        runButton = Button(this)
        runButton.text = "运行"
        runButton.isEnabled = false
        runButton.setOnClickListener { runLoaded() }

        logView = TextView(this)
        logView.textSize = 11f
        logView.setTextIsSelectable(true)

        val scroll = ScrollView(this)
        scroll.addView(logView)

        root.addView(openButton)
        root.addView(runButton)
        root.addView(
            scroll,
            LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f)
        )
        setContentView(root)
    }

    private fun log(text: String) {
        runOnUiThread {
            logView.append(text)
            logView.append("\n")
            (logView.parent as? ScrollView)?.post {
                (logView.parent as? ScrollView)?.fullScroll(ScrollView.FOCUS_DOWN)
            }
        }
    }

    private fun importAndLoad(uri: Uri) {
        if (job != null) {
            log("已有任务在执行")
            return
        }
        runButton.isEnabled = false
        val worker = Thread {
            try {
                val staging = File(cacheDir, "staging")
                staging.deleteRecursively()
                staging.mkdirs()
                val ipa = File(staging, "input.ipa")
                log("读取 " + uri)
                contentResolver.openInputStream(uri)?.use { input ->
                    ipa.outputStream().use { output -> input.copyTo(output) }
                } ?: run {
                    log("无法打开该文件")
                    return@Thread
                }
                log("已缓存 " + ipa.length() + " 字节")

                val work = File(cacheDir, "app")
                work.deleteRecursively()
                work.mkdirs()
                var count = 0
                ZipFile(ipa).use { zip ->
                    val entries = zip.entries()
                    while (entries.hasMoreElements()) {
                        val entry = entries.nextElement()
                        val name = entry.name
                        if (name.contains("..") || name.startsWith("/")) continue
                        val out = File(work, name)
                        if (entry.isDirectory) {
                            out.mkdirs()
                            continue
                        }
                        if (entry.size > MAX_ENTRY_BYTES) {
                            log("跳过超大条目 " + name)
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
                    log("IPA 里没有 Payload/*.app")
                    return@Thread
                }
                val exeName = readExecutable(File(app, "Info.plist"))
                if (exeName == null) {
                    log("Info.plist 里没有 CFBundleExecutable")
                    return@Thread
                }
                val exe = File(app, exeName)
                if (!exe.isFile) {
                    log("找不到可执行文件 " + exeName)
                    return@Thread
                }
                log("bundle " + app.name + " exe " + exeName)

                val report = Native.load(exe.absolutePath)
                report.split("\n").forEach { if (it.isNotEmpty()) log(it) }
                Native.report().split("\n").forEach { if (it.isNotEmpty() && !report.contains(it)) log(it) }
                runOnUiThread { runButton.isEnabled = true }
                log("已装载，可以点运行")
            } catch (t: Throwable) {
                log("失败 " + t)
            } finally {
                job = null
            }
        }
        job = worker
        worker.start()
    }

    private fun runLoaded() {
        if (job != null) {
            log("已有任务在执行")
            return
        }
        runButton.isEnabled = false
        val worker = Thread {
            try {
                log("执行主入口")
                log(Native.run())
                val pumped = Native.pump()
                if (pumped > 0) log("主队列执行 $pumped 个 block")
                Native.report().split("\n").forEach { if (it.isNotEmpty()) log(it) }
            } catch (t: Throwable) {
                log("运行时异常 " + t)
            } finally {
                runOnUiThread { runButton.isEnabled = true }
                job = null
            }
        }
        job = worker
        worker.start()
    }

    override fun onDestroy() {
        super.onDestroy()
        Native.release()
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

    companion object {
        private const val MAX_ENTRY_BYTES = 256L * 1024 * 1024
    }
}