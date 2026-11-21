# 实施记录

## Darwin ARM64 变参验收

`stringWithFormat:` 的格式串是固定参数，保留在 `x2`；该调用的匿名标量从调用方栈读取。普通派发和 `Super2` 不按选择器猜测参数布局。Foundation 自建类的元数据使用宿主指针，只有客机类数据使用 Darwin 地址掩码。

在 ARM64 Linux 上，使用当前 `chimera_run`、Clang 18 和 LLD 18 可运行 `sh tests/run_darwin_abi.sh /absolute/path/to/chimera_run`，不需要 Apple SDK。同一份路径测试 Mach-O 修复前返回 `11`，修复后返回 `0`。测试实际创建 `/var/mobile/Documents/profiles`，并逐字节比对目录中的 14 字节文件。另一个 Mach-O 验证了客机同名方法、`Super2`、浮点参数和间接返回值。

在断网 ARM64 虚拟机中，未修改的 CMCLauncher 可执行文件进入 `UIApplicationMain` 并返回 `0`；独立目录检查确认创建了 `mobile/Containers/Data/Application/App/Documents` 和 `latestlog.txt`。本次使用的是解包后的主程序，不是完整资源包；未验证 Android 首帧和交互。已观察到的后续缺口包括 `NSNumber` 装箱、集合初始化和字典下标写入。

## 此前的加载器修复

修复 Mach-O 旧格式绑定和镜像生命周期，加入原生回归测试。

已确认的旧实现问题：

- DO_BIND 不推进目标地址，连续绑定覆盖同一槽。
- 带偏移的绑定指令在写入前移动地址；重复绑定漏掉指针宽度。
- addend 被每次绑定后清零，lazy bind 和 rebase 未执行。
- 缺失符号返回通用函数地址，数据和函数被混淆。
- CF/NS/UI 等前缀统一被当作字符串；零填充对象并不实现对应框架。
- 最后一个未解析符号被当作崩溃原因，属于错误归因。
- 导入任务共用 input.ipa 和 payload，多次导入会互相覆盖。

## 验收顺序

1. Actions 原生回归测试覆盖连续绑定、跳跃、addend、弱引用、lazy 条目、rebase、非零 slide、越界和失败回滚。
2. 独立测试包校验目标 IPA 的绑定槽，不上传样本。
3. 独立运行进程和严格符号能力登记接入 APK。
4. 继续处理递归依赖、两级命名空间、ObjC ABI、框架和显示。

目标样本、日志、构建下载放在忽略目录；禁止上传用户 IPA。编译仅在 GitHub Actions 执行。通过占位符减少缺失计数不作为进展指标。
