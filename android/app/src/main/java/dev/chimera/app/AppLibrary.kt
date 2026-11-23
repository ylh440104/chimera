package dev.chimera.app

import android.util.AtomicFile
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.io.RandomAccessFile
import java.nio.channels.FileLock
import java.security.MessageDigest
import java.util.UUID
import java.util.zip.ZipFile

internal data class InstalledApp(val key: String, val identifier: String, val name: String, val version: String,
                                 val directory: File, val bundle: File, val executable: File, val icon: File?,
                                 val sandbox: File, val importedAt: Long)
internal data class LibrarySnapshot(val apps: List<InstalledApp>, val problems: List<String>)
internal class AppLease(private val file: RandomAccessFile, private val lock: FileLock) : AutoCloseable {
    override fun close() { try { lock.release() } finally { file.close() } }
}

internal class AppLibrary(private val files: File, private val cache: File) {
    private val home = File(files, "library")
    private val packages = File(home, "apps")
    private val data = File(files, "applications")

    fun snapshot(): LibrarySnapshot {
        val problems = mutableListOf<String>()
        val apps = packages.listFiles().orEmpty().filter { it.isDirectory && validKey(it.name) }.mapNotNull {
            try { find(it.name) } catch (e: Exception) {
                problems.add("${it.name.take(8)}：${e.message}")
                null
            }
        }.sortedWith(compareBy(String.CASE_INSENSITIVE_ORDER) { it.name })
        return LibrarySnapshot(apps, problems)
    }

    fun find(key: String): InstalledApp? {
        require(validKey(key)) { "应用记录无效" }
        val container = File(packages, key)
        val record = AtomicFile(File(container, "current.json"))
        if (!record.baseFile.exists() && !File(container, "current.json.bak").exists()) return null
        val json = record.openRead().use { JSONObject(it.readBytes().toString(Charsets.UTF_8)) }
        require(json.getInt("schema") == 1 && keyFor(json.getString("identifier")) == key) { "应用记录不匹配" }
        val generation = json.getString("generation")
        require(generation.matches(Regex("[a-f0-9-]{36}"))) { "安装目录无效" }
        val directory = child(container, generation)
        val bundle = child(directory, json.getString("bundle"))
        val exe = child(bundle, json.getString("executable"))
        require(exe.isFile && File(bundle, "Info.plist").isFile) { "应用文件缺失，请重新导入；数据仍保留" }
        val icon = json.optString("icon").takeIf { it.isNotEmpty() }?.let { child(bundle, it).takeIf(File::isFile) }
        return InstalledApp(key, json.getString("identifier"), json.getString("name"), json.optString("version"),
            directory, bundle, exe, icon, File(data, key), json.getLong("importedAt"))
    }

    fun acquire(key: String): AppLease {
        require(validKey(key))
        directory(data)
        val file = RandomAccessFile(File(data, "$key.lock"), "rw")
        try {
            val lock = file.channel.tryLock() ?: error("应用正在运行，请返回应用库后再操作")
            return AppLease(file, lock)
        } catch (e: Exception) {
            file.close()
            throw IllegalStateException("应用正在运行或暂时无法访问", e)
        }
    }

    fun install(input: InputStream, active: () -> Boolean = { true }, progress: (String) -> Unit = {}): InstalledApp =
        importArchive(input, true, active, progress)

    private fun importArchive(input: InputStream, replace: Boolean, active: () -> Boolean, progress: (String) -> Unit): InstalledApp {
        directory(home)
        val staging = File(home, ".import-${UUID.randomUUID()}")
        try {
            directory(staging)
            val ipa = File(staging, "source.ipa")
            progress("保存 IPA…")
            input.use { source ->
                FileOutputStream(ipa).use { output ->
                    val buffer = ByteArray(65536)
                    var size = 0L
                    while (true) {
                        check(active()) { "导入已停止" }
                        val n = source.read(buffer)
                        if (n < 0) break
                        size += n
                        require(size <= MAX_TOTAL) { "IPA 超出 1 GiB 限制" }
                        output.write(buffer, 0, n)
                    }
                    output.fd.sync()
                }
            }
            progress("解包并校验应用…")
            var total = 0L
            var count = 0
            val names = hashSetOf<String>()
            ZipFile(ipa).use { zip ->
                val entries = zip.entries()
                while (entries.hasMoreElements()) {
                    check(active()) { "导入已停止" }
                    val entry = entries.nextElement()
                    require(++count <= 60000) { "IPA 文件数量超出限制" }
                    val name = entry.name.trimEnd('/')
                    require(name != "source.ipa" && names.add(name)) { "IPA 含重复路径" }
                    val out = child(staging, name)
                    if (entry.isDirectory) directory(out) else {
                        require(entry.size <= MAX_ENTRY) { "单个文件超出 256 MiB 限制" }
                        directory(requireNotNull(out.parentFile))
                        zip.getInputStream(entry).use { source ->
                            out.outputStream().use { output ->
                                val buffer = ByteArray(65536)
                                var written = 0L
                                while (true) {
                                    check(active()) { "导入已停止" }
                                    val n = source.read(buffer)
                                    if (n < 0) break
                                    written += n
                                    total += n
                                    require(written <= MAX_ENTRY && total <= MAX_TOTAL) { "解包大小超出限制" }
                                    output.write(buffer, 0, n)
                                }
                            }
                        }
                    }
                }
            }
            return commit(staging, replace, active)
        } finally {
            input.close()
            staging.deleteRecursively()
        }
    }

    private fun commit(staging: File, replace: Boolean, active: () -> Boolean): InstalledApp {
        val bundles = File(staging, "Payload").listFiles().orEmpty().filter { it.isDirectory && it.name.endsWith(".app") }
        require(bundles.size == 1) { "IPA 必须包含一个主应用 Payload/*.app" }
        val bundle = bundles.single()
        val metadata = BundleMetadata.read(bundle)
        val executable = child(bundle, metadata.executable)
        require(executable.isFile && executable.length() >= 32) { "缺少可执行文件" }
        val magic = executable.inputStream().use { source -> List(4) { source.read().toByte() } }
        require(magic in listOf(listOf(0xcf, 0xfa, 0xed, 0xfe), listOf(0xca, 0xfe, 0xba, 0xbe),
            listOf(0xbe, 0xba, 0xfe, 0xca), listOf(0xca, 0xfe, 0xba, 0xbf)).map { it.map(Int::toByte) }) { "可执行文件不是 Mach-O" }
        val key = keyFor(metadata.identifier)
        acquire(key).use {
            if (!replace) find(key)?.let { return it }
            check(active()) { "导入已停止" }
            val container = File(packages, key)
            directory(container)
            val generation = UUID.randomUUID().toString()
            val destination = File(container, generation)
            require(File(destination, bundle.relativeTo(staging).path + "/" + metadata.executable).absolutePath.toByteArray().size < 512) { "应用路径过长" }
            val icon = findIcon(bundle, metadata.icons)?.relativeTo(bundle)?.invariantSeparatorsPath ?: ""
            val json = JSONObject().put("schema", 1).put("identifier", metadata.identifier)
                .put("name", metadata.name).put("version", metadata.version).put("generation", generation)
                .put("bundle", bundle.relativeTo(staging).invariantSeparatorsPath).put("executable", metadata.executable)
                .put("icon", icon).put("importedAt", System.currentTimeMillis())
            directory(File(data, key))
            File(data, "$key.bundle-id").writeText(metadata.identifier)
            check(staging.renameTo(destination)) { "无法保存应用文件" }
            writeRecord(File(container, "current.json"), json.toString())
            val saved = find(key)
            check(saved != null && saved.directory == destination) { "安装记录未写入，新旧程序文件均已保留" }
            container.listFiles().orEmpty().filter { it.isDirectory && it != destination && it.name.matches(Regex("[a-f0-9-]{36}")) }
                .forEach { it.deleteRecursively() }
            return saved
        }
    }

    fun migrateLegacy(active: () -> Boolean = { true }, progress: (String) -> Unit = {}): InstalledApp? {
        directory(home)
        val marker = File(home, "legacy-migrated")
        if (marker.exists()) return null
        val ipa = File(cache, "staging/input.ipa")
        val expanded = File(cache, "app")
        val migrated = when {
            ipa.isFile && ipa.length() > 0 -> importArchive(ipa.inputStream(), false, active, progress)
            File(expanded, "Payload").isDirectory -> {
                val staging = File(home, ".import-${UUID.randomUUID()}")
                try {
                    check(active()) { "迁移已停止" }
                    check(expanded.copyRecursively(staging, false)) { "旧应用迁移失败" }
                    commit(staging, false, active)
                } finally { staging.deleteRecursively() }
            }
            else -> return null
        }
        writeRecord(marker, migrated.key)
        ipa.delete()
        expanded.deleteRecursively()
        return migrated
    }

    fun remove(key: String, eraseData: Boolean) {
        acquire(key).use {
            val container = File(packages, key)
            check(!container.exists() || container.deleteRecursively()) { "未能移除安装文件" }
            if (eraseData) {
                val sandbox = File(data, key)
                check(!sandbox.exists() || sandbox.deleteRecursively()) { "未能删除应用数据" }
                File(data, "$key.bundle-id").delete()
            }
        }
    }

    private fun findIcon(bundle: File, icons: List<String>): File? {
        val candidates = icons.flatMap { name ->
            val stem = name.removeSuffix(".png")
            listOf("$stem@3x.png", "$stem@2x.png", "$stem.png", name)
        }
        return candidates.firstNotNullOfOrNull { name -> runCatching { child(bundle, name).takeIf { it.isFile && it.length() <= 4 * 1024 * 1024 } }.getOrNull() }
    }

    private fun writeRecord(file: File, text: String) {
        val atomic = AtomicFile(file)
        val output = atomic.startWrite()
        try {
            output.write(text.toByteArray(Charsets.UTF_8))
            atomic.finishWrite(output)
        } catch (e: Exception) {
            atomic.failWrite(output)
            throw e
        }
    }

    private fun child(base: File, name: String): File {
        require(name.isNotEmpty() && !name.startsWith('/') && !name.contains('\\') && !name.contains('\u0000') &&
            name.split('/').none { it.isEmpty() || it == "." || it == ".." }) { "IPA 包含非法路径" }
        val out = File(base, name)
        require(out.canonicalPath.startsWith(base.canonicalPath + File.separator)) { "路径超出应用目录" }
        return out
    }

    private fun directory(file: File) { check(file.isDirectory || file.mkdirs()) { "无法创建应用目录" } }
    private fun validKey(key: String) = key.matches(Regex("[0-9a-f]{64}"))

    companion object {
        private const val MAX_ENTRY = 256L * 1024 * 1024
        private const val MAX_TOTAL = 1024L * 1024 * 1024
        fun keyFor(identifier: String): String = MessageDigest.getInstance("SHA-256").digest(identifier.toByteArray(Charsets.UTF_8))
            .joinToString("") { "%02x".format(it.toInt() and 255) }
    }
}