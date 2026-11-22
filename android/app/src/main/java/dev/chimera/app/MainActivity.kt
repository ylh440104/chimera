package dev.chimera.app

import android.graphics.Bitmap
import android.net.Uri
import android.os.Bundle
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.ImageView
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
    private lateinit var frameView: ImageView
    private var bitmap: Bitmap? = null
    private var busy = false
    private var ready = false
    private var executed = false
    @Volatile private var closed = false

    private val picker = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) importAndLoad(uri)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val root = LinearLayout(this)
        root.orientation = LinearLayout.VERTICAL
        root.setPadding(32, 64, 32, 24)
        openButton = Button(this).apply {
            text = "导入 IPA"
            setOnClickListener { picker.launch(arrayOf("*/*")) }
        }
        runButton = Button(this).apply {
            text = "运行"
            isEnabled = false
            setOnClickListener { runLoaded() }
        }
        frameView = ImageView(this).apply {
            contentDescription = "客机首帧预览"
            scaleType = ImageView.ScaleType.FIT_CENTER
            visibility = View.GONE
        }
        logView = TextView(this).apply {
            textSize = 11f
            setTextIsSelectable(true)
        }
        val scroll = ScrollView(this)
        scroll.addView(logView)
        root.addView(openButton)
        root.addView(runButton)
        root.addView(frameView, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 2f))
        root.addView(scroll, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        setContentView(root)
    }

    private fun log(text: String) {
        runOnUiThread {
            if (!closed) {
                logView.append(text + "\n")
                (logView.parent as? ScrollView)?.post {
                    if (!closed) (logView.parent as? ScrollView)?.fullScroll(ScrollView.FOCUS_DOWN)
                }
            }
        }
    }

    private fun buttons() {
        openButton.isEnabled = !busy
        runButton.isEnabled = !busy && ready && !executed
    }

    private fun importAndLoad(uri: Uri) {
        if (busy || closed) return
        busy = true
        ready = false
        executed = false
        buttons()
        frameView.setImageDrawable(null)
        frameView.visibility = View.GONE
        bitmap?.recycle()
        bitmap = null
        logView.text = ""
        Native.executor.execute {
            var loaded = false
            try {
                if (!closed) {
                    Native.release()
                    val staging = File(cacheDir, "staging")
                    check(staging.isDirectory || staging.mkdirs())
                    val ipa = File(staging, "input.ipa")
                    log("读取 $uri")
                    val source = contentResolver.openInputStream(uri) ?: error("未能打开文件")
                    source.use { input -> ipa.outputStream().use { output -> input.copyTo(output) } }
                    log("已缓存 ${ipa.length()} 字节")
                    val work = File(cacheDir, "app")
                    check(!work.exists() || work.deleteRecursively())
                    check(work.mkdirs())
                    val base = work.canonicalPath + File.separator
                    var count = 0
                    var total = 0L
                    ZipFile(ipa).use { zip ->
                        val entries = zip.entries()
                        while (entries.hasMoreElements()) {
                            check(!closed) { "导入已停止" }
                            val entry = entries.nextElement()
                            val out = File(work, entry.name)
                            check(out.canonicalPath.startsWith(base)) { "条目超出应用目录" }
                            if (entry.isDirectory) {
                                check(out.isDirectory || out.mkdirs())
                                continue
                            }
                            check(entry.size <= MAX_ENTRY_BYTES) { "条目过大：${entry.name}" }
                            check(out.parentFile!!.isDirectory || out.parentFile!!.mkdirs())
                            zip.getInputStream(entry).use { input ->
                                out.outputStream().use { output ->
                                    val buffer = ByteArray(65536)
                                    var written = 0L
                                    while (true) {
                                        val n = input.read(buffer)
                                        if (n < 0) break
                                        written += n
                                        total += n
                                        check(written <= MAX_ENTRY_BYTES && total <= MAX_TOTAL_BYTES) { "解包大小超出限制" }
                                        output.write(buffer, 0, n)
                                    }
                                }
                            }
                            count++
                        }
                    }
                    log("解出 $count 个文件")
                    val app = File(work, "Payload").listFiles()?.firstOrNull { it.isDirectory && it.name.endsWith(".app") }
                        ?: error("IPA 里没有 Payload/*.app")
                    val exeName = readExecutable(File(app, "Info.plist")) ?: error("缺少 CFBundleExecutable")
                    val exe = File(app, exeName)
                    check(exe.isFile && exe.canonicalPath.startsWith(app.canonicalPath + File.separator)) { "可执行文件无效" }
                    log("bundle ${app.name} exe $exeName")
                    val sandbox = File(filesDir, "sandbox")
                    check(sandbox.isDirectory || sandbox.mkdirs()) { "应用数据目录创建失败" }
                    log(Native.load(exe.absolutePath, sandbox.absolutePath))
                    loaded = Native.ready()
                    log(if (loaded) "已装载，可以点运行" else "装载失败，运行已禁用")
                }
            } catch (e: Exception) {
                log("导入失败：$e")
            } finally {
                val value = loaded
                runOnUiThread {
                    if (!closed) { ready = value; busy = false; buttons() }
                }
            }
        }
    }

    private fun runLoaded() {
        if (busy || !ready || executed || closed) return
        busy = true
        executed = true
        buttons()
        Native.executor.execute {
            try {
                if (!closed) {
                    log("执行主入口")
                    val result = Native.run()
                    log(result)
                    if (result == "entry returned 0") {
                        val pumped = Native.pump()
                        if (pumped > 0) log("主队列执行 $pumped 个 block")
                        val painter = FramePainter(720, 1280)
                        var handedOff = false
                        try {
                            if (Native.paint(painter, 720, 1280)) {
                                File(filesDir, "last-frame.png").outputStream().use {
                                    check(painter.bitmap.compress(Bitmap.CompressFormat.PNG, 100, it))
                                }
                                File(filesDir, "last-frame-texts.txt").writeText(painter.texts.joinToString("\n"))
                                log("绘制完成：${painter.rectangles} 个色块，${painter.texts.size} 条文字；已保存 last-frame.png")
                                runOnUiThread {
                                    if (closed) painter.bitmap.recycle()
                                    else {
                                        bitmap = painter.bitmap
                                        frameView.setImageBitmap(painter.bitmap)
                                        frameView.visibility = View.VISIBLE
                                    }
                                }
                                handedOff = true
                            } else log("没有可绘制的客机窗口")
                        } finally {
                            if (!handedOff) painter.bitmap.recycle()
                        }
                    }
                    log(Native.report())
                }
            } catch (e: Exception) {
                log("运行异常：$e")
            } finally {
                runOnUiThread {
                    if (!closed) { busy = false; buttons() }
                }
            }
        }
    }

    override fun onDestroy() {
        closed = true
        Native.executor.execute { Native.release() }
        frameView.setImageDrawable(null)
        bitmap?.recycle()
        bitmap = null
        super.onDestroy()
    }

    private fun readExecutable(plist: File): String? {
        if (!plist.isFile || plist.length() > 2 * 1024 * 1024) return null
        val text = plist.readText()
        return Regex("<key>CFBundleExecutable</key>\\s*<string>([^<]+)</string>")
            .find(text)?.groupValues?.get(1)
    }

    companion object {
        private const val MAX_ENTRY_BYTES = 256L * 1024 * 1024
        private const val MAX_TOTAL_BYTES = 1024L * 1024 * 1024
    }
}
