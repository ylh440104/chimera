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
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream

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
                val work = File(cacheDir, "payload")
                work.deleteRecursively()
                work.mkdirs()

                log("解包 " + uri.lastPathSegment)
                extract(uri, work)

                val app = File(work, "Payload").listFiles()
                    ?.firstOrNull { it.isDirectory && it.name.endsWith(".app") }
                if (app == null) {
                    log("IPA 里没有 Payload/*.app")
                    return@Thread
                }

                val exeName = readExecutable(File(app, "Info.plist"))
                log("bundle " + app.name)
                if (exeName == null) {
                    log("Info.plist 里没有 CFBundleExecutable")
                    return@Thread
                }

                val exe = File(app, exeName)
                if (!exe.isFile) {
                    log("找不到可执行文件 " + exeName)
                    return@Thread
                }
                log("exe " + exeName + " " + exe.length() + " 字节")

                val rc = Native.run(exe.absolutePath)
                log("返回 " + rc)
            } catch (t: Throwable) {
                log("失败 " + t)
            }
        }.start()
    }

    private fun extract(uri: Uri, dir: File) {
        contentResolver.openInputStream(uri)?.use { input ->
            ZipInputStream(input).use { zip ->
                var entry: ZipEntry? = zip.nextEntry
                while (entry != null) {
                    val name = entry.name
                    if (name.contains("..")) {
                        zip.closeEntry()
                        entry = zip.nextEntry
                        continue
                    }
                    val out = File(dir, name)
                    if (entry.isDirectory) {
                        out.mkdirs()
                    } else {
                        out.parentFile?.mkdirs()
                        out.outputStream().use { os -> zip.copyTo(os) }
                    }
                    zip.closeEntry()
                    entry = zip.nextEntry
                }
            }
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