package dev.chimera.app

import android.util.Xml
import com.dd.plist.BinaryPropertyListParser
import org.xmlpull.v1.XmlPullParser
import java.io.File

internal data class BundleMetadata(val identifier: String, val name: String, val version: String,
                                   val executable: String, val icons: List<String>) {
    companion object {
        fun read(bundle: File): BundleMetadata {
            val plist = File(bundle, "Info.plist")
            require(plist.isFile && plist.length() in 1..2L * 1024 * 1024) { "Info.plist 缺失或过大" }
            val bytes = plist.readBytes()
            val binary = bytes.take(8).toByteArray().contentEquals("bplist00".toByteArray())
            val xml = if (binary) BinaryPropertyListParser.parse(bytes).toXMLPropertyList() else bytes.toString(Charsets.UTF_8)
            require(xml.toByteArray().size <= 2 * 1024 * 1024) { "Info.plist 过大" }
            val parser = Xml.newPullParser()
            parser.setInput(xml.reader())
            parser.nextTag()
            require(parser.name == "plist") { "无效的 Info.plist" }
            parser.nextTag()
            val root = value(parser, 0) as? Map<*, *> ?: error("Info.plist 必须是字典")
            fun string(key: String) = (root[key] as? String)?.trim()?.takeIf { it.isNotEmpty() }
            val id = string("CFBundleIdentifier") ?: error("缺少 CFBundleIdentifier")
            val exe = string("CFBundleExecutable") ?: error("缺少 CFBundleExecutable")
            require(id.length <= 255 && id.none { it.isISOControl() }) { "应用标识无效" }
            require(exe !in listOf(".", "..") && exe.none { it == '/' || it == '\\' || it.isISOControl() }) { "可执行文件名无效" }
            val icons = mutableListOf<String>()
            fun iconNames(dict: Map<*, *>?) {
                (dict?.get("CFBundleIconFiles") as? List<*>)?.filterIsInstance<String>()?.let { icons.addAll(it) }
                (dict?.get("CFBundleIconFile") as? String)?.let { icons.add(it) }
            }
            iconNames(root)
            val primary = (root["CFBundleIcons"] as? Map<*, *>)?.get("CFBundlePrimaryIcon") as? Map<*, *>
            iconNames(primary)
            if (binary) plist.writeText(xml)
            return BundleMetadata(id, string("CFBundleDisplayName") ?: string("CFBundleName") ?: bundle.nameWithoutExtension,
                string("CFBundleShortVersionString") ?: string("CFBundleVersion") ?: "", exe, icons.distinct())
        }

        private fun value(parser: XmlPullParser, depth: Int): Any? {
            require(depth < 48) { "Info.plist 嵌套过深" }
            return when (parser.name) {
                "dict" -> {
                    val result = linkedMapOf<String, Any?>()
                    while (parser.nextTag() == XmlPullParser.START_TAG) {
                        require(parser.name == "key") { "无效的 plist 字典" }
                        val key = parser.nextText()
                        require(parser.nextTag() == XmlPullParser.START_TAG) { "缺少 plist 值" }
                        result[key] = value(parser, depth + 1)
                    }
                    result
                }
                "array" -> {
                    val result = mutableListOf<Any?>()
                    while (parser.nextTag() == XmlPullParser.START_TAG) result.add(value(parser, depth + 1))
                    result
                }
                else -> parser.nextText()
            }
        }
    }
}
