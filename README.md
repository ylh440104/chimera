# Chimera

导入 IPA，在 Android 上直接运行。

Chimera 是 Darwin 用户态兼容层：不模拟硬件，不重写 UIKit，而是把真实的 arm64 iOS 框架二进制加载进来，自己提供内核 ABI、运行时和显示服务端。

## 分层与现状

    L0  Mach-O 加载器      通  段映射、chained fixups、dyld_info 绑定
    L1  Darwin ABI        部分  基础符号替身、TLS、mmap/fcntl 翻译
    L2  运行时            骨架  objc4 最小实现 + libdispatch 队列
    L3  框架              清单  依赖登记，二进制未搬运
    L4  显示              桩    CA render server 未实现
    L5  IPA 入口          通   SAF 导入、解包、定位主二进制、报告

跑通的部分：选择 IPA、解包、读 Info.plist、定位主二进制、解析 Mach-O、映射到 0x100000000、完成全部导入符号绑定、跳入入口执行，崩溃被捕获成报告。

## 代码结构

    include/chm.h         Mach-O 结构与加载器接口
    include/chm_rt.h      失败捕获、TLS、加密检测
    include/chm_int.h     运行时/框架/UI 层接口与报告缓冲

    src/macho.c           load command、段映射、fixup、符号表、dump
    src/shim.c            导入符号表、数据符号、TLS、信号捕获、加密检测
    src/loader.c          入口定位与跳转
    src/objc.c            objc4 最小实现：类表、方法查找、msgSend、ARC 替身
    src/dispatch.c        libdispatch 队列：async/sync/once/main_q
    src/fw.c              框架依赖分类：替身 / 待搬运 / 未知
    src/ui.c              UIApplicationMain 调用入口
    src/report.c          报告缓冲

    tools/chmdump.c       镜像结构打印
    tools/chmrun.c        加载并执行
    tools/probe.c         带信号诊断的加载器
    tools/mkfixture.py    构造最小 arm64 Mach-O 测试件
    tests/hello.c         macOS runner 上编译成真品 iPhoneOS 二进制
    tests/execprobe.c     Android 匿名可执行内存可行性验证

    android/              Gradle 工程，包名 dev.chimera.app

## 运行时进展

objc 层已能：

- 扫描 `__objc_classlist` 建立类表，读出类名
- `objc_getClass` / `objc_getMetaClass` / `sel_registerName`
- 方法列表解析（classic 与 relative/small 两种格式）
- `objc_msgSend` / `objc_msgSendSuper2` 的方法查找与转发
- ARC 替身：retain/release/autorelease/storeStrong/retainBlock
- `objc_alloc` 按 instance_size 分配并填入 isa
- `+load` 遍历调用

已知限制：msgSend 只按 6 个整数参数转发，浮点参数场景不覆盖；Swift 类元数据未处理。

dispatch 层已能：`__dispatch_main_q`、async（入队）、sync、once、get_main_queue、get_global_queue，队列由 `chm_dispatch_pump` 驱动。

框架层：`fw.c` 把 22 个依赖分成替身（libSystem/libobjc/libc++/libdispatch）、待搬运（Swift/Foundation/UIKit/QuartzCore/Metal/Security 等）、未知三类，报告里会打印前几项。

## 构建

    sh build.sh
    python3 tools/mkfixture.py
    ./build/chmdump tests/fixture.ios
    ./build/chmrun tests/fixture.ios

APK 由 GitHub Actions 的 `apk` workflow 构建，产物为 `chimera-debug-apk`。

## 报告示例

   镜像: 段 5, dylib 22, 符号 4279, 入口 0xa6b4
   fw: 待搬运 /System/Library/Frameworks/UIKit.framework/UIKit
   fw: 依赖 22 个，替身 8，待搬运 12，未知 2
   已映射并绑定
   objc: 类 37
   objc: 例 CMCAppDelegate CMCSceneDelegate ...
   执行中断: signal 11 at 0x0 after _OBJC_METACLASS_$_UITabBarController
   缺 12 个符号: _kSecAttrAccount ...

## 下一步

P3 收尾  objc：swift 类元数据、category、协议、异常展开
P4      框架搬运：Foundation / QuartzCore 的 arm64 二进制加载与符号两级命名空间
P5      显示：CA render server，图层树落 SurfaceFlinger
P6      IPA 打磨：解密检测、平台打标、UIApplicationMain 完整启动

## 已知限制

- 只处理 arm64 切片，arm64e 的 auth 指针按普通指针处理
- 不加载 dyld shared cache，目标是模拟器运行时的松散 dylib
- App Store 下载的 IPA 有 FairPlay 加密，必须先在越狱设备解密
- 目前不能显示界面，执行会在框架符号处中断