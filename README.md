<p align="center">
  <img src="docs/assets/chimera.png" width="160" height="160" alt="Chimera">
</p>

<h1 align="center">Chimera</h1>

<p align="center">Android 上的 IPA 应用库与 Darwin 用户态运行环境</p>

<p align="center">
  <a href="https://github.com/ylh440104/chimera/releases">下载 APK</a> ·
  <a href="https://github.com/ylh440104/chimera/actions/workflows/apk.yml">Android 构建</a> ·
  <a href="https://github.com/ylh440104/chimera/actions/workflows/native.yml">原生测试</a> ·
  <a href="https://github.com/ylh440104/chimera/issues">问题反馈</a>
</p>

Chimera 将 IPA 导入 Android 本地应用库，再从应用列表选择并运行。每个应用拥有独立的程序文件和客机数据目录，重新打开 Chimera 后不需要重复导入。运行页面使用全屏客机画面，支持点击、拖动、网络请求、文本输入和运行日志导出。

项目的底层是一个面向 arm64 Darwin 程序的用户态兼容层。它读取 Mach-O 镜像，处理段映射、重定位、绑定和 Objective-C 元数据，并为客机提供文件、偏好、网络、队列、UIKit 窗口和绘制服务。Android 侧负责应用管理、生命周期、输入分发、硬件 Canvas 重放和公共下载目录写入。

## 核心功能

### IPA 应用库

- 使用 Android 系统文件选择器一次选择多个 IPA。
- 导入时保存原始 IPA，并在应用库中保留解包后的 Payload。
- 从 `Info.plist` 读取 Bundle ID、显示名称、版本、主程序和图标。
- 首页显示已导入应用的图标、名称、版本和 Bundle ID。
- 点按「打开」进入对应应用，长按应用可管理或移除。
- 以 Bundle ID 的 SHA-256 作为应用键，同名但不同 Bundle ID 的应用互不覆盖。
- 相同 Bundle ID 重新导入时发布新的安装代际，同时继续使用原数据目录。
- 覆盖安装、清除缓存和删除下载目录中的原始 IPA 不会删除已保存的应用库。

### 全屏客机窗口

点击应用库中的「打开」后，Chimera 会启动独立的客机 Activity，并隐藏系统栏。右上角的 **⋮** 菜单包含两个操作：

| 操作 | 行为 |
| --- | --- |
| 应用库 | 释放当前客机会话和应用锁，返回应用列表 |
| 导出日志 | 将当前会话日志写入公共 `Download` 目录 |

客机画面按实际窗口尺寸创建视口。旋转或改变窗口方向后，UIKit 根窗口和 Android 绘制视口同步更新，触摸坐标会转换回客机坐标。

Android 显示层使用硬件加速 `View` 直接重放绘制指令，不创建整屏中间 Bitmap。`Choreographer` 驱动刷新，客机绘制指令由 `FramePainter` 转换为 Android Canvas 操作。

### 输入与网络

- 点击和拖动事件映射到客机 UIKit 坐标。
- 客机文本字段使用 Android 输入框完成编辑，再将结果提交回客机。
- 主队列由 Android 刷新循环驱动，后台队列由原生运行时处理。
- HTTP 和 HTTPS 请求由 Android `HttpURLConnection` 执行。
- 请求状态、响应头、响应体、下载进度和取消结果回到客机执行线程。
- 单个网络响应上限为 32 MiB；IPA 单文件上限为 256 MiB，总导入数据上限为 1 GiB。

### 日志导出

每次打开应用会创建独立的运行日志，包含：

- 应用名称、版本和 Bundle ID。
- 北京时间的会话开始时间。
- 加载、运行、输入、网络和绘制事件。
- 最新的原生会话报告。

日志使用 UTF-8 编码，文件名格式如下：

```text
Chimera-log-YYYYMMDD-HHmmss-xxxxxxxx.txt
```

Android 10 及以上使用 `MediaStore.Downloads` 发布文件；Android 8 和 Android 9 使用公共 `Download` 目录，并在首次导出时申请写入权限。导出不会覆盖已有日志，写入失败时会清理本次创建的条目。

## 安装和使用

1. 从 [Releases](https://github.com/ylh440104/chimera/releases) 下载 APK。
2. 安装并打开 Chimera。
3. 点击「+ 导入 IPA」，选择一个或多个 IPA 文件。
4. 等待导入完成，应用会出现在应用库列表中。
5. 点击目标应用的「打开」进入全屏运行页面。
6. 需要切换应用时，点击右上角 **⋮ → 应用库**。
7. 需要提交运行信息时，点击右上角 **⋮ → 导出日志**。

应用库不依赖原始 IPA 的存放位置。导入完成后，可以移动或删除下载目录中的原始 IPA。

## 数据与持久化

Chimera 将应用程序文件和客机数据分开保存。目录位于应用私有存储中，外部应用无法直接访问。

```text
files/
├── library/
│   └── apps/
│       └── <Bundle ID 的 SHA-256>/
│           ├── current.json
│           └── <安装代际 UUID>/
│               ├── source.ipa
│               └── Payload/
│                   └── <主应用>.app/
└── applications/
    └── <Bundle ID 的 SHA-256>/
        ├── <客机 Documents、Library 等数据>
        └── <应用锁与 Bundle ID 记录>
```

`current.json` 记录当前安装代际、Bundle ID、显示名称、版本、主 Bundle 路径、可执行文件名和图标路径。文件使用 `AtomicFile` 写入，安装记录写入并读回成功后才会清理旧程序代际。

### 数据保留规则

| Android 或应用操作 | 程序文件 | 客机数据 |
| --- | --- | --- |
| 退出运行页面 | 保留 | 保留 |
| 重新打开 Chimera | 保留 | 保留 |
| 清除 Android 缓存 | 保留 | 保留 |
| 重新导入同一 Bundle ID | 更新 | 保留 |
| 应用库移除，默认选项 | 删除 | 保留 |
| 移除时选择同时删除数据 | 删除 | 删除对应应用数据 |
| Android 清除存储 | 删除 | 删除 |
| 卸载 Chimera | 删除 | 删除 |

持久化针对已经写入客机文件系统的内容。运行中的内存、当前画面和未写入文件的状态不会被保存为快照。

## 导入校验

为了避免坏包或不完整导入破坏已有安装，导入过程使用临时目录，并依次执行：

1. 将输入流保存为临时 `source.ipa`。
2. 检查 ZIP 条目数量、重复路径、路径穿越和文件大小。
3. 解包并确认存在唯一的 `Payload/*.app` 主应用。
4. 读取 XML 或二进制 `Info.plist`。
5. 检查主程序存在、大小有效，并验证 Mach-O 文件头。
6. 依据 Bundle ID 获取应用锁。
7. 将临时安装目录改名为新的安装代际。
8. 原子写入 `current.json`，重新读取并核对记录。
9. 仅在新记录确认有效后删除旧安装代际。

导入中断、输入流失败、清单无效、路径非法或主程序校验失败时，旧安装和已有客机数据保持不变。

## 运行链路

```text
Android 文件选择器
        │
        ▼
LibraryActivity
        │  多选导入、应用列表、迁移、移除
        ▼
AppLibrary
        │  临时导入、校验、代际发布、数据隔离
        ▼
MainActivity :guest
        │  应用锁、全屏窗口、输入、网络、日志
        ▼
JNI bridge
        │  Native.load / run / tick / paint / tap / pan
        ▼
Darwin 用户态兼容层
        │
        ├── Mach-O 加载与 fixups
        ├── Objective-C 类、选择器与消息派发
        ├── Foundation 文件、JSON、plist、偏好与通知
        ├── dispatch 主队列与后台队列
        ├── UIKit 窗口、控制器、表格、弹窗与触摸
        └── Android 网络和 Canvas 服务
```

## 兼容接口

当前代码覆盖的客机功能包括：

- Mach-O arm64 镜像读取、段映射、重定位、绑定和符号登记。
- Objective-C 类表、元类、选择器、方法列表、消息派发和基础 ARC 行为。
- Darwin 文件路径、应用主目录、Documents、Library、临时目录和文件流。
- XML plist、二进制 plist、JSON 序列化与解析。
- `NSUserDefaults`、原子文件写入、目录枚举、排序和通知中心。
- `UIApplicationMain`、窗口、根控制器、导航控制器、标签栏控制器和模态控制器。
- `UIView`、标签、按钮、表格、开关、滚动视图、文本字段和警告框。
- 约束布局、表格分组、单元格复用、选中回调、开关事件和滚动命中。
- `NSURLSession` 数据任务的请求提交、响应、错误、取消和下载进度。
- Android 硬件 Canvas 绘制、客机刷新、点击、拖动和文本编辑桥接。

程序依赖的接口取决于具体 IPA。Chimera 会根据实际加载和运行结果生成原生报告；FairPlay 加密的 App Store 程序需要先获得可执行的解密版本。

## 项目结构

```text
.github/workflows/
├── apk.yml                  Android 测试、NDK 构建、签名和 Release 发布
├── native.yml               ARM64 原生构建与 Darwin 回归
├── extract.yml              macOS 模拟器运行库提取任务
└── probe.yml                macOS 运行库信息探测任务

android/
├── app/src/main/java/dev/chimera/app/
│   ├── AppLibrary.kt        应用库、安装代际、迁移和数据锁
│   ├── BundleMetadata.kt    IPA 主清单和图标元数据
│   ├── LibraryActivity.kt   应用列表和多选导入页面
│   ├── MainActivity.kt      全屏客机窗口和运行会话
│   ├── GuestScreen.kt       全屏画面和右上角菜单
│   ├── GuestImageView.kt    Canvas 绘制与触摸坐标转换
│   ├── GuestNetwork.kt      HTTP(S) 请求和下载回调
│   ├── DownloadLog.kt       Download 日志导出
│   └── Native.kt            JNI 方法声明
├── app/src/main/jni/        JNI、CMake 和 Android 原生入口
├── app/src/main/res/        应用图标和 Android 资源
├── app/src/main/assets/     随 APK 分发的第三方许可
└── app/src/test/            JUnit、Robolectric 和应用库测试

include/
└── chm_session.h            加载器、运行会话和 UIKit 服务接口

src/
├── macho.c、fixups.c        Mach-O 读取和修复
├── session.c、loader.c      镜像会话和入口执行
├── objc.c、objc_arm64.S     Objective-C 运行时
├── foundation.c             Foundation 对象和文件服务
├── dispatch.c               队列和主线程泵
├── uikit.c                  UIKit 对象、窗口和控制器
├── ui_*.inc                 绘制、输入、表格、弹窗和组件实现
├── scene.c                  场景配置和生命周期
└── frameworks.c             系统框架符号和常量桥接

tests/
├── darwin_*.m               Darwin ARM64 客机测试程序
├── test_*.c                 原生运行时回归
└── run_darwin_abi.sh        客机测试编译和执行脚本

tools/
├── chmdump.c                Mach-O 结构查看工具
├── chmdemand.c              依赖和符号分析工具
├── chimera_run.c            宿主运行工具
└── mkfixture.py             测试镜像生成工具

third_party/yxml/
├── yxml.c、yxml.h           XML tokenizer
├── LICENSE                  yxml MIT 许可
└── UPSTREAM.md              来源和源码校验说明
```

Android APK 的 yxml 许可位于 `android/app/src/main/assets/licenses/yxml.txt`。

```text
docs/
├── android-signing.md       固定签名和 CI 密钥说明
└── assets/                  README 和项目展示资源
```

## 构建环境

Android 构建使用：

| 工具 | 版本 |
| --- | --- |
| JDK | 17 |
| Gradle | 8.9 |
| Android Gradle Plugin | 8.5.2 |
| Kotlin | 2.0.20 |
| Android SDK / Build Tools | 34 / 34.0.0 |
| Android NDK | 26.1.10909125 |
| CMake | 3.22.1 |
| Android ABI | `arm64-v8a` |

应用配置：

- Application ID：`dev.chimera.app`
- Namespace：`dev.chimera.app`
- 最低 Android：API 26
- Target SDK：34
- 入口 Activity：`dev.chimera.app.LibraryActivity`
- 客机 Activity 进程：`dev.chimera.app:guest`
- 版本码：`GITHUB_RUN_NUMBER`，本地没有该变量时使用 `1`

## Android 构建

固定签名构建需要以下环境变量：

```text
CHIMERA_KEYSTORE_PATH
CHIMERA_KEYSTORE_PASSWORD
```

GitHub Actions 额外使用以下 Secrets：

```text
CHIMERA_SIGNING_STORE
CHIMERA_SIGNING_PASSWORD
CHIMERA_SIGNING_SHA256
```

本地命令：

```sh
cd android
gradle :app:testDebugUnitTest :app:assembleDebug --no-daemon
```

APK 输出路径：

```text
android/app/build/outputs/apk/debug/app-debug.apk
```

工作流会执行 Android 单元测试、CMake/NDK 构建、固定证书校验、SHA-256 计算，并将 APK、提交号、证书信息和校验文件上传到 artifact，同时发布到 `apk-<run_id>` Release。

固定签名版本可以覆盖安装。更换签名来源时，Android 会拒绝覆盖安装；卸载应用会清除应用私有目录中的应用库和客机数据。

## 原生构建与测试

原生工作流运行于 `ubuntu-24.04-arm`，使用系统 C 编译器编译 Mach-O 工具、运行时和测试程序，并使用 Clang 18 与 LLD 18 构建 Darwin ARM64 测试程序。

工作流覆盖：

- Mach-O fixups、绑定、重定位、弱引用和越界输入。
- 场景清单、Bundle 配置和 XML plist 读取。
- Objective-C 调用、变参、主队列和后台队列。
- 文件、目录、JSON、plist、偏好、通知和应用路径。
- UIKit 控件、表格、弹窗、文本输入、滚动和触摸。
- HTTP 任务、响应、取消、下载文件和进度回调。
- 720×1280 竖屏与 1600×720 横屏视口切换。

原生构建产物和测试日志会保存为 GitHub Actions artifact。项目不把用户 IPA 或客机私有数据提交到仓库。

## 依赖和许可

- AndroidX AppCompat、Activity：Android 页面、生命周期和系统窗口。
- dd-plist：读取 XML 和二进制 `Info.plist`。
- JUnit、Robolectric：Android 单元测试和视图行为测试。
- Clang、LLD：构建 Darwin ARM64 测试程序。
- yxml：读取 XML plist，来源和许可见 [`third_party/yxml/UPSTREAM.md`](third_party/yxml/UPSTREAM.md) 与 [`third_party/yxml/LICENSE`](third_party/yxml/LICENSE)。

## 反馈问题

反馈运行问题时，请一并提供：

1. Chimera APK 版本或 Git 提交号。
2. Android 版本、设备 ABI 和应用版本。
3. 导入、打开或操作的具体步骤。
4. 右上角菜单导出的 `Chimera-log-*.txt`。
5. 如果问题与特定 IPA 有关，请说明 Bundle ID；不要上传包含个人数据的 IPA。

问题请提交到 [GitHub Issues](https://github.com/ylh440104/chimera/issues)。