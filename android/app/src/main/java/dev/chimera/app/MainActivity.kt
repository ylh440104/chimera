package dev.chimera.app

import android.net.Uri
import android.os.Bundle
import android.view.View
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
    private lateinit var frameView: GuestImageView
    private var frame: GuestFrame? = null
    private var busy = false
    private var ready = false
    private var executed = false
    private var guestLive = false
    @Volatile private var closed = false
    @Volatile private var serviceEpoch = 0L
    private var editorToken = 0L
    private var editor: androidx.appcompat.app.AlertDialog? = null
    private val eventHandler = android.os.Handler(android.os.Looper.getMainLooper())
    private val eventTick = object : Runnable {
        override fun run() {
            if (closed) return
            if (!busy && guestLive && editorToken == 0L) {
                val epoch = serviceEpoch
                Native.executor.execute {
                    try {
                        if (!closed && epoch == serviceEpoch) {
                            val count = Native.pump()
                            check(count >= 0) { "客机异步回调异常" }
                            if (count > 0) drawGuestFrame()
                            else serviceOutputs()
                        }
                    } catch (e: Exception) {
                        log("异步处理失败：$e")
                        runOnUiThread { guestLive = false; buttons() }
                    } finally {
                        if (!closed) eventHandler.postDelayed(this, 200)
                    }
                }
            } else eventHandler.postDelayed(this, 200)
        }
    }

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
        frameView = GuestImageView(this).apply {
            contentDescription = "客机画面"
            visibility = View.GONE
            onGuestTap = { from, to -> tapGuest(from.x, from.y, to.x, to.y) }
            onGuestScroll = { from, delta -> scrollGuest(from.x, from.y, delta) }
            onBackend = { hardware ->
                log(if (hardware) "屏幕绘制：GPU 硬件 Canvas" else "屏幕绘制：软件 Canvas")
                Native.executor.execute {
                    File(filesDir, "render-backend.txt").writeText("hardwareCanvas=$hardware\nbitmapRaster=false\n")
                }
            }
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
        eventHandler.postDelayed(eventTick, 200)
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
        frameView.acceptInput = !closed && !busy && guestLive && frameView.hasFrame
    }

    private fun importAndLoad(uri: Uri) {
        if (busy || closed) return
        serviceEpoch++
        editor?.dismiss()
        editor = null
        editorToken = 0
        busy = true
        ready = false
        executed = false
        guestLive = false
        buttons()
        frameView.submitFrame(null)
        frameView.visibility = View.GONE
        frame = null
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
                        check(pumped >= 0) { "客机队列异常，重新导入后再运行" }
                        if (pumped > 0) log("主队列执行 $pumped 个 block")
                        check(drawGuestFrame()) { "没有可绘制的客机窗口" }
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

    private fun drawGuestFrame(): Boolean {
        val painter = FramePainter(720, 1280)
        if (!Native.paint(painter, 720, 1280)) return false
        val next = painter.finish()
        File(filesDir, "last-frame-texts.txt").writeText(next.texts.joinToString("\n"))
        log("提交画面：${next.rectangles} 个色块，${next.texts.size} 条文字")
        runOnUiThread {
            if (!closed) {
                frame = next
                frameView.submitFrame(next)
                frameView.visibility = View.VISIBLE
                guestLive = true
                buttons()
            }
        }
        serviceOutputs()
        return true
    }

    private fun serviceOutputs() {
        val epoch = serviceEpoch
        while (!closed && epoch == serviceEpoch) {
            val request = Native.nextRequest() ?: break
            GuestNetwork.send(request) { result ->
                if (!closed && epoch == serviceEpoch) {
                    try {
                        val rc = Native.completeRequest(result.request.token, result.status, result.body, result.headers, result.error)
                        check(rc >= 0) { "网络完成回调异常" }
                        if (rc > 0) {
                            check(Native.pump() >= 0)
                            check(drawGuestFrame())
                            File(filesDir, "last-report.txt").writeText(Native.report())
                        }
                    } catch (e: Exception) {
                        log("网络回调失败：$e")
                        runOnUiThread { guestLive = false; buttons() }
                    }
                }
            }
        }
        val token = Native.focusedField()
        if (token == 0L) return
        val text = Native.fieldText(false).toString(Charsets.UTF_8)
        val hint = Native.fieldText(true).toString(Charsets.UTF_8)
        val options = Native.fieldOptions()
        runOnUiThread {
            if (!closed && epoch == serviceEpoch && editorToken == 0L) {
                editorToken = token
                val field = android.widget.EditText(this).apply {
                    setSingleLine(true)
                    inputType = when (options and 255) {
                        4, 5 -> android.text.InputType.TYPE_CLASS_NUMBER or android.text.InputType.TYPE_NUMBER_FLAG_SIGNED
                        8 -> android.text.InputType.TYPE_CLASS_NUMBER or android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL
                        7 -> android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS
                        3 -> android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_VARIATION_URI
                        else -> android.text.InputType.TYPE_CLASS_TEXT
                    }
                    if (options and 256 != 0) {
                        inputType = android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_VARIATION_PASSWORD
                    }
                    setText(text)
                    this.hint = hint
                    setSelection(length())
                }
                var resolved = false
                fun finishEdit(commit: Boolean) {
                    if (resolved) return
                    resolved = true
                    editorToken = 0
                    if (!closed && epoch == serviceEpoch) interactGuest("编辑输入框") {
                        Native.editText(token, field.text.toString().toByteArray(Charsets.UTF_8), commit)
                    }
                }
                editor = androidx.appcompat.app.AlertDialog.Builder(this)
                    .setView(field)
                    .setPositiveButton(android.R.string.ok) { _, _ -> finishEdit(true) }
                    .setNegativeButton(android.R.string.cancel) { _, _ -> finishEdit(false) }
                    .setOnCancelListener { finishEdit(false) }
                    .create()
                editor?.setOnShowListener {
                    field.requestFocus()
                    editor?.window?.setSoftInputMode(android.view.WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE)
                }
                editor?.show()
            }
        }
    }

    private fun tapGuest(downX: Float, downY: Float, upX: Float, upY: Float) {
        interactGuest("点击客机 (${upX.toInt()}, ${upY.toInt()})") {
            Native.tap(downX.toDouble(), downY.toDouble(), upX.toDouble(), upY.toDouble())
        }
    }

    private fun scrollGuest(x: Float, y: Float, delta: Float) {
        interactGuest("滚动客机 ${delta.toInt()}") {
            Native.scroll(x.toDouble(), y.toDouble(), delta.toDouble())
        }
    }

    private fun interactGuest(label: String, action: () -> Int) {
        if (closed || busy || !guestLive) return
        busy = true
        buttons()
        Native.executor.execute {
            var healthy = true
            try {
                if (!closed) {
                    val rc = action()
                    log("$label，结果 $rc")
                    check(rc >= 0) { "点击回调异常，已停止输入；重新导入可重建会话" }
                    val pumped = Native.pump()
                    check(pumped >= 0) { "客机队列异常，已停止输入" }
                    check(drawGuestFrame()) { "点击后没有可绘制窗口" }
                }
            } catch (e: Exception) {
                healthy = false
                log("交互异常：$e")
            } finally {
                try {
                    if (!closed) {
                        val report = Native.report()
                        File(filesDir, "last-report.txt").writeText(report)
                        if (!healthy) log(report)
                    }
                } catch (e: Exception) {
                    log("保存诊断失败：$e")
                }
                val ok = healthy
                runOnUiThread {
                    if (!closed) {
                        if (!ok) guestLive = false
                        busy = false
                        buttons()
                    }
                }
            }
        }
    }

    override fun onDestroy() {
        closed = true
        serviceEpoch++
        eventHandler.removeCallbacks(eventTick)
        editor?.dismiss()
        editor = null
        Native.executor.execute { Native.release() }
        frameView.submitFrame(null)
        frame = null
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
