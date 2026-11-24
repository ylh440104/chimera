package dev.chimera.app

import android.content.Intent
import android.content.res.Configuration
import android.graphics.BitmapFactory
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.CheckBox
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import java.util.concurrent.Executors

internal object LibraryWorker {
    val executor = Executors.newSingleThreadExecutor()
}

class LibraryActivity : AppCompatActivity() {
    private val library by lazy { AppLibrary(filesDir, cacheDir) }
    private lateinit var rows: LinearLayout
    private lateinit var count: TextView
    private lateinit var status: TextView
    private lateinit var add: Button
    private lateinit var progress: ProgressBar
    private var busy = false
    private var openingGuest = false
    @Volatile private var closed = false
    private val dark get() = resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK == Configuration.UI_MODE_NIGHT_YES
    private val ink get() = if (dark) 0xfff4f5f7.toInt() else 0xff172033.toInt()
    private val secondary get() = if (dark) 0xffa5abba.toInt() else 0xff737b8c.toInt()
    private val surface get() = if (dark) 0xff20242d.toInt() else Color.WHITE
    private val backgroundColor get() = if (dark) 0xff13161d.toInt() else 0xfff3f5f9.toInt()
    private val accent = 0xff3478f6.toInt()
    private val picker = registerForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isNotEmpty()) importApps(uris)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        supportActionBar?.hide()
        window.statusBarColor = backgroundColor
        window.navigationBarColor = backgroundColor
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(20), dp(16), dp(20), dp(12))
            setBackgroundColor(backgroundColor)
        }
        val header = LinearLayout(this).apply { gravity = Gravity.CENTER_VERTICAL }
        header.addView(text("应用库", 30f, ink).apply { setTypeface(typeface, Typeface.BOLD) }, LinearLayout.LayoutParams(0, dp(56), 1f))
        add = Button(this).apply {
            text = "+ 导入 IPA"
            isAllCaps = false
            setTextColor(Color.WHITE)
            background = rounded(accent, 16)
            setOnClickListener { if (!busy) picker.launch(arrayOf("*/*")) }
        }
        header.addView(add, LinearLayout.LayoutParams(dp(116), dp(46)))
        root.addView(header)
        root.addView(text("导入一次，随时打开。应用数据独立保存。", 13f, secondary).apply { setPadding(0, dp(6), 0, dp(18)) })
        count = text("已安装", 13f, secondary)
        root.addView(count)
        progress = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply {
            isIndeterminate = true
            visibility = View.GONE
        }
        root.addView(progress, LinearLayout.LayoutParams(-1, dp(3)).apply { topMargin = dp(10) })
        status = text("", 13f, secondary).apply { setPadding(0, dp(12), 0, dp(8)) }
        root.addView(status)
        rows = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        root.addView(ScrollView(this).apply { isFillViewport = true; addView(rows) }, LinearLayout.LayoutParams(-1, 0, 1f))
        setContentView(root)
        refresh(true)
    }

    override fun onResume() {
        super.onResume()
        openingGuest = false
        if (!busy) refresh(false)
    }

    private fun refresh(migrate: Boolean) {
        if (busy || closed) return
        setBusy(true)
        LibraryWorker.executor.execute {
            val messages = mutableListOf<String>()
            try {
                if (migrate) {
                    library.migrateLegacy({ !closed }) { updateStatus("迁移上次导入的应用 · $it") }
                        ?.let { messages.add("${it.name} 已加入应用库，原有数据已保留") }
                }
            } catch (e: Exception) { messages.add("旧应用迁移失败：${e.message}。可重新导入，原有数据会保留。") }
            publish(messages)
        }
    }

    private fun importApps(uris: List<Uri>) {
        if (busy || closed) return
        setBusy(true)
        LibraryWorker.executor.execute {
            val messages = mutableListOf<String>()
            for ((i, uri) in uris.withIndex()) {
                if (closed) break
                try {
                    val input = contentResolver.openInputStream(uri) ?: error("无法读取 IPA")
                    val app = library.install(input, { !closed }) { updateStatus("${i + 1}/${uris.size} · $it") }
                    messages.add("${app.name} 已保存")
                } catch (e: Exception) { messages.add("第 ${i + 1} 项导入失败：${e.message}") }
            }
            publish(messages)
        }
    }

    private fun publish(messages: List<String>) {
        val result = library.snapshot()
        runOnUiThread {
            if (!closed) {
                render(result.apps)
                status.text = (messages + result.problems).joinToString("\n")
                setBusy(false)
            }
        }
    }

    private fun setBusy(value: Boolean) {
        busy = value
        add.isEnabled = !value
        progress.visibility = if (value) View.VISIBLE else View.GONE
        rows.alpha = if (value) 0.6f else 1f
    }

    private fun updateStatus(message: String) { runOnUiThread { if (!closed) status.text = message } }

    private fun render(apps: List<InstalledApp>) {
        rows.removeAllViews()
        count.text = "已安装 ${apps.size} 个应用"
        if (apps.isEmpty()) {
            rows.addView(text("还没有应用\n\n点击右上角导入 IPA，可以一次选择多个文件。", 16f, secondary).apply {
                gravity = Gravity.CENTER
                setPadding(dp(20), dp(70), dp(20), dp(40))
            }, LinearLayout.LayoutParams(-1, -2))
        }
        apps.forEach { app ->
            val row = LinearLayout(this).apply {
                gravity = Gravity.CENTER_VERTICAL
                setPadding(dp(14), dp(16), dp(8), dp(16))
                background = rounded(surface, 20)
                contentDescription = "${app.name}，${app.version}，打开应用"
                setOnClickListener { launch(app) }
                setOnLongClickListener { if (!busy) manage(app); true }
            }
            val icon = app.icon?.let { file ->
                runCatching {
                    val options = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                    BitmapFactory.decodeFile(file.absolutePath, options)
                    if (options.outWidth <= 0 || options.outHeight <= 0) null else {
                        options.inSampleSize = 1
                        while (options.outWidth / options.inSampleSize > 160 || options.outHeight / options.inSampleSize > 160) options.inSampleSize *= 2
                        options.inJustDecodeBounds = false
                        BitmapFactory.decodeFile(file.absolutePath, options)
                    }
                }.getOrNull()
            }
            val tile = if (icon == null) text(app.name.take(1).uppercase(), 26f, accent).apply {
                gravity = Gravity.CENTER
                background = rounded(if (dark) 0xff293b5a.toInt() else 0xffe8effe.toInt(), 14)
            } else ImageView(this).apply {
                setImageBitmap(icon)
                scaleType = ImageView.ScaleType.FIT_CENTER
                background = rounded(surface, 14)
                clipToOutline = true
            }
            row.addView(tile, LinearLayout.LayoutParams(dp(56), dp(56)))
            val details = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(dp(14), 0, dp(6), 0)
                addView(text(app.name, 17f, ink).apply { setTypeface(typeface, Typeface.BOLD); maxLines = 2 })
                addView(text(if (app.version.isEmpty()) "已安装" else "版本 ${app.version}", 12f, secondary).apply { setPadding(0, dp(4), 0, 0) })
                addView(text(app.identifier, 11f, secondary).apply { maxLines = 1; ellipsize = android.text.TextUtils.TruncateAt.END })
            }
            row.addView(details, LinearLayout.LayoutParams(0, -2, 1f))
            row.addView(Button(this).apply {
                text = "打开"
                isAllCaps = false
                setTextColor(accent)
                setOnClickListener { launch(app) }
                setOnLongClickListener { if (!busy) manage(app); true }
            }, LinearLayout.LayoutParams(dp(74), dp(48)))
            rows.addView(row, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = dp(12) })
        }
    }

    private fun launch(app: InstalledApp) {
        if (busy || openingGuest) return
        openingGuest = true
        startActivity(Intent(this, MainActivity::class.java).putExtra("app_key", app.key))
    }

    private fun manage(app: InstalledApp) {
        val erase = CheckBox(this).apply {
            text = "同时删除此应用的所有数据"
            isChecked = false
            setPadding(dp(20), dp(8), dp(20), dp(8))
        }
        AlertDialog.Builder(this).setTitle(app.name)
            .setMessage("移除安装文件后可重新导入。默认保留存档、设置和下载文件。")
            .setView(erase).setNegativeButton("取消", null).setPositiveButton("移除应用") { _, _ ->
                if (!busy) {
                    val deleteData = erase.isChecked
                    setBusy(true)
                    LibraryWorker.executor.execute {
                        val message = try {
                            library.remove(app.key, deleteData)
                            if (deleteData) "${app.name} 及其数据已移除" else "${app.name} 已移除，数据已保留"
                        } catch (e: Exception) { "移除失败：${e.message}" }
                        publish(listOf(message))
                    }
                }
            }.show()
    }

    private fun dp(value: Int) = (value * resources.displayMetrics.density + 0.5f).toInt()
    private fun text(value: String, size: Float, color: Int) = TextView(this).apply { text = value; textSize = size; setTextColor(color) }
    private fun rounded(color: Int, radius: Int) = GradientDrawable().apply { setColor(color); cornerRadius = dp(radius).toFloat() }

    override fun onDestroy() {
        closed = true
        super.onDestroy()
    }
}