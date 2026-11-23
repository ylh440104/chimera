package dev.chimera.app

import android.content.Intent
import android.os.Bundle
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import java.io.File

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
    private val choreographer by lazy { android.view.Choreographer.getInstance() }
    @Volatile private var foreground = false
    private var frameScheduled = false
    private var tickInFlight = false
    private var servicesChanged = false
    private var lastTransferReport = 0L
    private var networkInFlight = 0
    private var gpuAvailable = true
    private val frameCallback = android.view.Choreographer.FrameCallback { nanos -> onVsync(nanos) }

    private fun scheduleFrame() {
        val wanted = foreground && !closed && !busy && guestLive && gpuAvailable && editorToken == 0L
        if (wanted && !frameScheduled && !tickInFlight) {
            frameScheduled = true
            choreographer.postFrameCallback(frameCallback)
        } else if (!wanted && frameScheduled) {
            frameScheduled = false
            choreographer.removeFrameCallback(frameCallback)
        }
    }

    private fun onVsync(nanos: Long) {
        frameScheduled = false
        if (!foreground || closed || busy || !guestLive || !gpuAvailable || editorToken != 0L) return
        val epoch = serviceEpoch
        val first = frame == null
        val hz = frameView.display?.refreshRate?.takeIf { it > 0 && it.isFinite() } ?: 60f
        tickInFlight = true
        Native.executor.execute {
            try {
                if (foreground && !closed && epoch == serviceEpoch) {
                    val callbacks = Native.tick(nanos, 1.0 / hz)
                    check(callbacks >= 0) { "客机显示或队列回调异常" }
                    if (first || callbacks > 0 || servicesChanged) {
                        servicesChanged = false
                        drawGuestFrame(epoch, diagnostic = first)
                    } else serviceOutputs(epoch)
                }
            } catch (e: Exception) {
                failGuest(epoch, "刷新失败", e)
            } finally {
                runOnUiThread { tickInFlight = false; scheduleFrame() }
            }
        }
    }

    private var lease: AppLease? = null
    private lateinit var appKey: String
    private val library by lazy { AppLibrary(filesDir, cacheDir) }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val root = LinearLayout(this)
        root.orientation = LinearLayout.VERTICAL
        root.setPadding(32, 64, 32, 24)
        openButton = Button(this).apply {
            text = "‹ 应用库"
            setOnClickListener { finish() }
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
            onGuestPan = { phase, x, y ->
                if (phase == PanPhase.BEGIN) panEpoch = serviceEpoch
                if (!closed && panEpoch == serviceEpoch) panQueue.offer(PanEvent(panEpoch, phase, x, y))
            }
            onBackend = { hardware ->
                gpuAvailable = hardware
                buttons()
                log(if (hardware) "GPU 硬件 Canvas · 屏幕同步刷新" else "当前窗口不支持硬件 Canvas，已暂停客机绘制")
                Native.executor.execute {
                    File(filesDir, "render-backend.txt").writeText("hardwareCanvas=$hardware\nbitmapRaster=false\nframeClock=Choreographer\n")
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
        val key = intent.getStringExtra("app_key")
        if (key == null) {
            startActivity(Intent(this, LibraryActivity::class.java))
            finish()
        } else {
            appKey = key
            loadInstalled()
        }
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
        frameView.acceptInput = foreground && !closed && !busy && guestLive && gpuAvailable && frameView.hasFrame
        scheduleFrame()
    }

    private fun loadInstalled() {
        if (busy || closed) return
        serviceEpoch++
        val epoch = serviceEpoch
        busy = true
        ready = false
        executed = false
        guestLive = false
        buttons()
        Native.executor.execute {
            var loaded = false
            try {
                if (!closed) {
                    lease = library.acquire(appKey)
                    val app = library.find(appKey) ?: error("应用未安装，请从应用库重新导入")
                    check(app.sandbox.isDirectory || app.sandbox.mkdirs()) { "无法打开应用数据目录" }
                    log("打开 ${app.name} ${app.version}")
                    log(Native.load(app.executable.absolutePath, app.sandbox.absolutePath))
                    loaded = Native.ready()
                    if (!loaded) log("此应用尚不能运行，安装文件及数据已保留")
                }
            } catch (e: Exception) {
                log("打开失败：${e.message}")
            } finally {
                val value = loaded
                runOnUiThread {
                    if (!closed && epoch == serviceEpoch) {
                        ready = value
                        busy = false
                        buttons()
                        if (value) runLoaded()
                    }
                }
            }
        }
    }

    private fun runLoaded() {
        if (busy || !ready || executed || closed) return
        busy = true
        executed = true
        val epoch = serviceEpoch
        buttons()
        Native.executor.execute {
            try {
                if (!closed) {
                    log("执行主入口")
                    val result = Native.run()
                    log(result)
                    if (result == "entry returned 0") {
                        log("准备绘制客机首帧")
                        if (!drawGuestFrame(epoch)) log("等待客机创建窗口，继续处理队列")
                        runOnUiThread {
                            if (!closed && epoch == serviceEpoch) { guestLive = true; buttons() }
                        }
                    }
                    val report = Native.report()
                    log(report)
                    File(filesDir, "last-report.txt").writeText(report)
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

    private fun drawGuestFrame(epoch: Long, diagnostic: Boolean = true): Boolean {
        if (closed || epoch != serviceEpoch) return false
        val painter = FramePainter(720, 1280)
        val status = Native.paint(painter, 720, 1280)
        check(status >= 0) { "客机绘制失败（$status）" }
        if (status == 0) {
            serviceOutputs(epoch)
            return false
        }
        val next = painter.finish()
        runOnUiThread {
            if (!closed && epoch == serviceEpoch) {
                frame = next
                frameView.submitFrame(next)
                frameView.visibility = View.VISIBLE
                buttons()
            }
        }
        if (diagnostic) {
            log("提交画面：${next.rectangles} 个色块，${next.texts.size} 条文字")
            try {
                File(filesDir, "last-frame-texts.txt").writeText(next.texts.joinToString("\n"))
            } catch (e: java.io.IOException) {
                log("保存画面诊断失败：$e")
            }
        }
        serviceOutputs(epoch)
        return true
    }

    private fun failGuest(epoch: Long, label: String, e: Exception) {
        if (closed || epoch != serviceEpoch) return
        val report = Native.report()
        runCatching { File(filesDir, "last-report.txt").writeText(report) }
        runOnUiThread {
            if (!closed && epoch == serviceEpoch) {
                guestLive = false
                buttons()
                log("$label：$e\n$report")
            }
        }
    }

    private fun serviceOutputs(epoch: Long) {
        while (!closed && epoch == serviceEpoch && networkInFlight < 4) {
            val request = Native.nextRequest() ?: break
            networkInFlight++
            val active = { !closed && epoch == serviceEpoch }
            GuestNetwork.send(request, active, { done, total ->
                if (!active()) false else Native.executor.submit<Boolean> {
                    if (!active()) false else {
                        val rc = Native.transferProgress(request.token, done, total)
                        if (rc < 0) failGuest(epoch, "下载进度回调异常", IllegalStateException("$rc"))
                        if (rc > 0) servicesChanged = true
                        rc > 0
                    }
                }.get()
            }) { result ->
                networkInFlight--
                if (active()) {
                    try {
                        val rc = Native.completeRequest(request.token, result.status, result.body, result.headers, result.error)
                        check(rc >= 0) { "网络完成回调异常" }
                        if (rc > 0) {
                            servicesChanged = true
                            val now = android.os.SystemClock.elapsedRealtime()
                            val failed = result.error.isNotEmpty() || result.status >= 400
                            if (failed) log("请求 #${request.token} 失败（HTTP ${result.status}）${result.error}")
                            if (failed || now - lastTransferReport > 1000 || networkInFlight == 0) {
                                File(filesDir, "last-report.txt").writeText(Native.report())
                                lastTransferReport = now
                            }
                            if (foreground) serviceOutputs(epoch)
                        }
                    } catch (e: Exception) { failGuest(epoch, "网络回调失败", e) }
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

    private var panEpoch = -1L
    private val panQueue = PanQueue(Native.executor) { event ->
        if (!closed && event.epoch == serviceEpoch) {
            try {
                val changed = Native.pan(event.phase.code, event.x.toDouble(), event.y.toDouble())
                check(changed >= 0) { "拖动回调异常" }
                if (changed > 0) drawGuestFrame(event.epoch, diagnostic = event.phase != PanPhase.MOVE)
            } catch (e: Exception) {
                runOnUiThread {
                    if (!closed && event.epoch == serviceEpoch && guestLive) {
                        log("滚动失败：$e")
                        guestLive = false
                        buttons()
                    }
                }
            }
        }
    }

    private fun interactGuest(label: String, action: () -> Int) {
        if (closed || busy || !guestLive) return
        val epoch = serviceEpoch
        busy = true
        buttons()
        Native.executor.execute {
            var healthy = true
            try {
                if (!closed) {
                    val rc = action()
                    log("$label，结果 $rc")
                    check(rc >= 0) { "点击回调异常，请返回应用库后重新打开" }
                    val pumped = Native.pump()
                    check(pumped >= 0) { "客机队列异常，已停止输入" }
                    drawGuestFrame(epoch)
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

    override fun onResume() {
        super.onResume()
        foreground = true
        buttons()
    }

    override fun onPause() {
        foreground = false
        buttons()
        super.onPause()
    }

    override fun onDestroy() {
        closed = true
        serviceEpoch++
        scheduleFrame()
        editor?.dismiss()
        editor = null
        Native.executor.execute {
            try { Native.release() } finally { lease?.close(); lease = null }
        }
        frameView.submitFrame(null)
        frame = null
        super.onDestroy()
    }

}
