# Chimera

导入 IPA，在 Android 上直接运行。

Chimera 是 Darwin 用户态兼容层：不模拟硬件，不重写 UIKit，而是把真实的 arm64 iOS
框架二进制加载进来，自己提供内核 ABI、运行时和显示服务端。

## 与同类项目的关系

- Darling：Darwin/macOS 在 Linux 上的兼容层，提供了 Mach-O 加载器和 darlingserver
  式的系统调用翻译范式。Chimera 沿用它的 L0/L1 分层与 mach 端口表设计。
- PlayCover：在 Apple Silicon Mac 上运行 App Store 解密 IPA，价值在“重打包 + 平台
  标记改写 + 运行时 shim”这套流程。Chimera 的 IPA 入口走同样思路。
- touchHLE：高级模拟路线的先行者，手写 UIKit 子集顶替 iPhone OS。Chimera 把它当作
  框架搬不动时的降级路径。
- Husk：反方向（Android 应用跑在 iOS 上）的完整实现，框架替身的分层可以直接镜像。
- qemu-ios：全系统模拟参考实现，用来核对用户态依赖哪些守护进程。
- WinObjC：现存最完整的 UIKit 重实现，作为 API 面参考。

## 分层

    L0  Mach-O 加载器     load command、段映射、chained fixups、dyld_info 绑定
    L1  Darwin ABI        BSD 系统调用与 mach trap 翻译到 bionic
    L2  运行时            objc4、libdispatch、Swift runtime、libunwind
    L3  框架              搬运 arm64 iOS 框架二进制
    L4  显示              CoreAnimation render server 对接 SurfaceFlinger
    L5  胶水              IPA 解包、平台打标、沙盒路径、设备信息、输入映射

## 当前状态

P1 已跑通，宿主为 aarch64 Linux：

    sh build.sh
    python3 tools/mkfixture.py
    ./build/chmdump tests/fixture.ios
    ./build/chmrun tests/fixture.ios

输出：

    chimera: hello from fixture
    chimera: main returned 0

这一步验证了完整链路：解析 arm64 Mach-O、映射 __TEXT/__DATA/__LINKEDIT、解释
dyld_info 的 bind opcode、把 `_write` 绑到宿主 shim、跳到 LC_MAIN 入口、按 Darwin
arm64 调用约定传参、执行并返回。

APK 侧的骨架已经就位：选择 IPA、解包到 cacheDir、从 Info.plist 读
CFBundleExecutable、定位主二进制、调用 native 加载器。构建在 GitHub Actions 完成。

    android/             Gradle 工程，包名 dev.chimera.app
    android/app/src/main/jni/chimera_jni.c   JNI 入口，直接复用 src/ 下的加载器

## 目录

    include/chm.h        Mach-O 结构定义与加载器接口
    src/macho.c          load command、段映射、fixup、符号表、dump
    src/shim.c           导入符号表（write/read/errno/malloc/memcpy/exit 等）
    src/loader.c         入口定位与跳转
    tools/chmdump.c      镜像结构打印
    tools/chmrun.c       加载并执行
    tools/probe.c        带信号诊断的加载器
    tools/mkfixture.py   构造最小 arm64 Mach-O 测试件
    tests/hello.c        留待 macOS runner 编译成真品 iPhoneOS 二进制
    tests/execprobe.c    Android 匿名可执行内存可行性验证

## 下一步

P2  dylib 加载、两级命名空间、弱符号、TLS（TPIDRRO_EL0 与 tlv_get_addr）
P3  objc4 编进 Android，跑通 objc_msgSend、类与方法解析
P4  搬运 Foundation 与 QuartzCore，补齐 libdispatch 的 mach 垫片
P5  CoreAnimation render server，图层树落到 SurfaceFlinger
P6  IPA 入口打磨：解密检测、平台打标、启动 UIApplicationMain

## 已知限制

- 只处理 arm64 切片；arm64e 的 auth 指针按普通指针处理。
- 目前不加载 dyld shared cache，模拟器运行时的松散 dylib 才是目标来源。
- App Store 下载的 IPA 有 FairPlay 加密，必须先在越狱设备上解密。