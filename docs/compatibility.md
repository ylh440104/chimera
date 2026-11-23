# 兼容范围与验证

## 原则

Chimera 按 Objective-C、UIKit、Foundation 的接口实现能力，不依据 IPA 文件名、Bundle ID、客机类名或页面文字切换行为。主 Bundle 的元数据从导入包的 Info.plist 读取，资源子 Bundle 使用自己的路径。Android 数据目录按 Bundle ID 的 SHA-256 分开；相同 Bundle ID 的升级保留同一目录，不同 ID 不共享偏好和账户文件。

## 已实现并由独立客机回归覆盖

- 主场景选择、窗口、根控制器、导航栈、标签页和模态隔离。
- 等式布局、文字、表格分组与单元格、选择回调、开关和滚动后命中。
- UIAlertController、UIAlertAction、输入字段、禁用按钮、编辑取消和提交。标题、文字、按钮及回调由客机提供。
- 栈 Block 复制、捕获值和 `__block` 转发。主队列由界面事件循环驱动，全局队列和自建队列使用工作线程。客机代码串行访问运行时，在管道读写等待期间让出执行权。
- JSON 解析和序列化、XML plist 的字典/数组/字符串/数值/布尔值、文件输出流、原子保存、目录枚举、排序和 NSUserDefaults 持久化。
- NSNotificationCenter 的名称/对象过滤、发送和移除观察者。
- NSURLSession 数据任务的请求提交、状态、成功/错误/取消完成回调。原生 CI 注入测试响应，不把它记为公网请求验收。
- UTF-16 常量字符串到 UTF-8 的转换，包含中文与代理对 emoji。
- Bundle 元数据、资源子目录、基础 Localizable.strings 查找、可变数组插入和删除。

Android 使用硬件加速 View Canvas 重放绘制指令，不再通过 Canvas(Bitmap) 生成整屏图片。实际后端由 canvas.isHardwareAccelerated 记录到 render-backend.txt；布局、文字排版和事件判断仍由 CPU 执行。HTTP(S) 数据任务由 Android HttpURLConnection 执行，结果回到同一个客机执行线程。每个数据任务响应上限为 32 MiB。

## 文件路径与 Darwin ABI

`NSHomeDirectory` 和 Documents 查询使用客机的绝对 `HOME`。改变 `HOME` 不等于改变当前目录；`NSFileManager` 切换当前目录后，相对路径的 `open`、`access`、`stat`、`opendir` 和 `fopen` 使用同一套映射。POSIX 创建文件不会顺带创建缺少的父目录。

`tests/darwin_files.m` 从客机两端验证同一个文件：先通过 Documents URL 原子写入，再用文件描述符和标准文件流读回；反向用 `open` 创建、追加，由 `NSData` 和 `NSFileManager` 核对字节、大小和权限。测试按 arm64 Darwin 的字段偏移读取 `stat`，并在 4 GiB 以上定位后验证末尾字节和 `fclose` 的所有权。宿主另外检查两个文件的实际位置、内容及权限。这组用例不覆盖完整文件系统、归档安装或所有 `open` 标志；没有宿主数据的 `stat` 出生时间和 Darwin 专有标志保留为零。

## 验收证据

提交 `c5e90e9` 的客机测试在手指上移 100 点、尚未抬起时检查目标行及图标，再点选该行核对回调。Android 测试覆盖拖动中替换显示帧、合并 MOVE、着色与裁剪。文件测试通过 Documents URL 写入 10 字节，用相对路径和文件 URL 分别读回，并检查文件大小、权限与修改时间。[原生回归](https://github.com/ylh440104/chimera/actions/runs/45108042738) · [Android 测试与签名构建](https://github.com/ylh440104/chimera/actions/runs/45108042723)

以相同的 CMCLauncher 主程序、libAFNetworking 和 UnzipKit 分别运行两个版本的 `chmdemand`。对比 CSV 的 `symbol` 列，`8c452ae` 有 113 个不同的未解析符号，`c5e90e9` 有 84 个，减少 29 个，新增 0 个。这项检查统计镜像绑定结果。

实现提交 eb5cfbd 的原生 workflow 为 45028523665。tests/darwin_services.m 和 tests/test_services.c 验证了弹窗输入、栈回调、文件读写、Unicode、通知和模拟网络完成；原有布局、容器、点击与表格回归同时运行。tests/services-info.plist 使用独立 Bundle 标识，验证不依赖目标启动器的元数据。

用户要求停止点屏幕后，后续验证限于静态检查和 GitHub Actions。新版原始 IPA 的联网列表、创建账户、安装版本和启动游戏尚未完成真机端到端验收。不能用 CI 通过替代这一结论。

## 尚未覆盖

- 任意 IPA、完整 iOS API、SwiftUI、WebKit、Metal、Core Animation、全部 CoreFoundation 语义。
- 二进制 plist、全部字符串编码与本地化规则；当前导入元数据只支持 XML plist。
- 完整 ARC/对象释放、跨线程 libdispatch、任务取消的底层连接中断。
- NSURLSession 委托式下载任务、后台下载、完整 AFNetworking 依赖、大文件安装与解压链路。
- 微软登录浏览器流程、文档选择器和外部应用联动。
- JVM、动态运行库和 Minecraft 图形运行的完整链路。
- 通用图片资源、SF Symbols 和复杂自定义控件绘制。

旧版共用 files/sandbox 目录仍保留。新目录不会自动把无 Bundle 归属标记的旧数据复制给任意新导入 IPA，避免跨应用污染。
