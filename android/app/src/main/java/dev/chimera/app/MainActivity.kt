package dev.chimera.app

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.view.WindowManager
import android.widget.Toast
import androidx.activity.addCallback
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import java.io.File

class MainActivity : AppCompatActivity() {
    private lateinit var screen: GuestScreen
    private val frameView get() = screen.image
    @Volatile private var viewport: GuestViewport? = null
    private val sessionLog = GuestSessionLog()
    private var exporting = false
    private val storagePermission = registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        if (!closed) {
            if (granted) exportSessionLog() else toast("需要存储权限才能将日志写入 Download")
        }
    }
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
        val first = !frameFitsViewport()
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
        WindowCompat.setDecorFitsSystemWindows(window, false)
        if (Build.VERSION.SDK_INT >= 28) {
            window.attributes = window.attributes.apply {
                layoutInDisplayCutoutMode = if (Build.VERSION.SDK_INT >= 30)
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                else WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
            }
        }
        screen = GuestScreen(this, ::returnToLibrary, ::requestLogExport)
        hideSystemBars()
        frameView.apply {
            onGuestTap = { from, to -> tapGuest(from.x, from.y, to.x, to.y) }
            onGuestPan = { phase, x, y ->
                if (phase == PanPhase.BEGIN) panEpoch = serviceEpoch
                if (!closed && panEpoch == serviceEpoch) panQueue.offer(PanEvent(panEpoch, phase, x, y))
            }
            onBackend = { hardware ->
                gpuAvailable = hardware
                if (!hardware) showFailure(serviceEpoch, "当前窗口无法绘制客机画面")
                updateGuestInput()
                log(if (hardware) "GPU 硬件 Canvas · 屏幕同步刷新" else "当前窗口不支持硬件 Canvas，已暂停客机绘制")
                Native.executor.execute {
                    File(filesDir, "render-backend.txt").writeText("hardwareCanvas=$hardware\nbitmapRaster=false\nframeClock=Choreographer\n")
                }
            }
        }
        setContentView(screen)
        onBackPressedDispatcher.addCallback(this) { returnToLibrary() }
        val key = intent.getStringExtra("app_key")
        if (key == null) {
            returnToLibrary()
            return
        }
        appKey = key
        log("正在打开应用")
        frameView.addOnLayoutChangeListener { view, _, _, _, _, _, _, _, _ ->
            if (!closed && view.width > 0 && view.height > 0) {
                val previous = viewport
                val next = GuestViewport.fromPixels(view.width, view.height)
                if (next != previous) {
                    viewport = next
                    if (previous == null) loadInstalled() else updateGuestInput()
                }
            }
        }
    }

    private fun hideSystemBars() {
        WindowCompat.getInsetsController(window, window.decorView).apply {
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            hide(WindowInsetsCompat.Type.systemBars())
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus && !closed) hideSystemBars()
    }

    private fun returnToLibrary() {
        if (closed) return
        closeSession()
        if (isTaskRoot) startActivity(Intent(this, LibraryActivity::class.java))
        finish()
    }

    private fun log(text: String) = sessionLog.append(text)

    private fun saveReport(report: String) {
        sessionLog.updateReport(report)
        try {
            File(filesDir, "last-report.txt").writeText(report)
        } catch (e: java.io.IOException) {
            log("保存内部报告失败：${e.message}")
        }
    }

    private fun toast(text: String) = Toast.makeText(applicationContext, text, Toast.LENGTH_LONG).show()

    private fun requestLogExport() {
        if (closed || exporting) return
        if (Build.VERSION.SDK_INT < 29 && checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
            storagePermission.launch(Manifest.permission.WRITE_EXTERNAL_STORAGE)
        } else exportSessionLog()
    }

    private fun exportSessionLog() {
        if (closed || exporting) return
        exporting = true
        val epoch = serviceEpoch
        val nativeReport = if (ready) Native.executor.submit<String?> {
            if (!closed && serviceEpoch == epoch) Native.report() else null
        } else null
        DownloadLog.executor.execute {
            val latest = try {
                nativeReport?.get(500, java.util.concurrent.TimeUnit.MILLISECONDS)
            } catch (e: Exception) {
                nativeReport?.cancel(false)
                null
            }
            val message = try {
                "日志已保存到 ${DownloadLog.save(applicationContext, sessionLog.snapshot(latest))}"
            } catch (e: Exception) {
                "导出日志失败：${e.localizedMessage ?: "无法写入 Download"}"
            }
            runOnUiThread { exporting = false; toast(message) }
        }
    }

    private fun updateGuestInput() {
        frameView.acceptInput = foreground && !closed && !busy && guestLive && gpuAvailable && frameFitsViewport()
        scheduleFrame()
    }

    private fun frameFitsViewport(): Boolean {
        val current = frame ?: return false
        val size = viewport ?: return false
        return current.width == size.width && current.height == size.height
    }

    private fun showFailure(epoch: Long, message: String) {
        runOnUiThread {
            if (!closed && epoch == serviceEpoch) screen.showStatus("$message\n可从右上角导出日志")
        }
    }

    private fun loadInstalled() {
        if (busy || closed) return
        serviceEpoch++
        val epoch = serviceEpoch
        busy = true
        ready = false
        executed = false
        guestLive = false
        updateGuestInput()
        Native.executor.execute {
            var loaded = false
            try {
                if (!closed) {
                    lease = library.acquire(appKey)
                    val app = library.find(appKey) ?: error("应用未安装，请从应用库重新导入")
                    check(app.sandbox.isDirectory || app.sandbox.mkdirs()) { "无法打开应用数据目录" }
                    sessionLog.identify(app.name, app.version, app.identifier)
                    val size = requireNotNull(viewport)
                    log("打开 ${app.name} ${app.version}，画面 ${size.width} × ${size.height}")
                    sessionLog.updateReport(Native.load(app.executable.absolutePath, app.sandbox.absolutePath, size.width, size.height))
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
                        updateGuestInput()
                        if (value) runLoaded() else showFailure(epoch, "应用未能启动")
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
        updateGuestInput()
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
                            if (!closed && epoch == serviceEpoch) { guestLive = true; updateGuestInput() }
                        }
                    } else showFailure(epoch, "应用运行已停止")
                    saveReport(Native.report())
                }
            } catch (e: Exception) {
                log("运行异常：$e")
                showFailure(epoch, "应用运行异常")
            } finally {
                runOnUiThread {
                    if (!closed) { busy = false; updateGuestInput() }
                }
            }
        }
    }

    private fun drawGuestFrame(epoch: Long, diagnostic: Boolean = true): Boolean {
        if (closed || epoch != serviceEpoch) return false
        val size = viewport ?: return false
        val painter = FramePainter(size.width, size.height)
        val status = Native.paint(painter, size.width, size.height)
        check(status >= 0) { "客机绘制失败（$status）" }
        if (status == 0) {
            serviceOutputs(epoch)
            return false
        }
        val next = painter.finish()
        runOnUiThread {
            if (!closed && epoch == serviceEpoch && size == viewport) {
                frame = next
                frameView.submitFrame(next)
                if (gpuAvailable) screen.showStatus(null)
                updateGuestInput()
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
        saveReport(Native.report())
        log("$label：$e")
        runOnUiThread {
            if (!closed && epoch == serviceEpoch) {
                guestLive = false
                updateGuestInput()
                showFailure(epoch, label)
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
                                saveReport(Native.report())
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
                failGuest(event.epoch, "滚动失败", e)
            }
        }
    }

    private fun interactGuest(label: String, action: () -> Int) {
        if (closed || busy || !guestLive) return
        val epoch = serviceEpoch
        busy = true
        updateGuestInput()
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
                        saveReport(Native.report())
                    }
                } catch (e: Exception) {
                    log("保存诊断失败：$e")
                }
                val ok = healthy
                runOnUiThread {
                    if (!closed) {
                        if (!ok) {
                            guestLive = false
                            showFailure(epoch, "客机交互已停止")
                        }
                        busy = false
                        updateGuestInput()
                    }
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        foreground = true
        updateGuestInput()
    }

    override fun onPause() {
        foreground = false
        updateGuestInput()
        super.onPause()
    }

    private fun closeSession() {
        if (closed) return
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
    }

    override fun onDestroy() {
        closeSession()
        super.onDestroy()
    }

}
