# UTType 行为对照

`uttype-reference.jsonl` 是 Apple 系统接口的原始输出，不是 Chimera 生成的期望值。

- 探针源码：`.github/reference/uttype.m`。
- 来源提交：`57ea3a65275414d3c914763b0eca64a5822b2f78`。
- GitHub Actions run：45528759710，artifact：14983927253。
- 环境：macOS 15.8.2，arm64，SDK 15.5，Apple clang 17.0.0。
- 采集时间：2026-11-25 09:51:19，北京时间。
- 文件 SHA-256：`e8213e6126a8942f298d694bfd0197116d0db51d834fe9728a6e9d06a2ed1c0c`。

共 124 条记录：1 条环境、106 条标签查询和 17 条类型查询。该次任务在输出后的 Python 检查失败，原因是把 JSON 数字 1 与布尔单例 True 作身份比较；探针已运行完毕，stderr 为空。工作流现已修正此检查。

`test_uttype` 接受第三个参数指定对照文件。每个会话比较 122 条查询的标识、首选扩展名及 MIME；含嵌入 NUL 的一条记录由 `darwin_uttype.m` 以字节和长度断言独立验证，避免依赖当前 JSON 解析器的 NUL 限制。默认连续运行三个独立会话。

本批实现仅涵盖两个 C 查询接口、内建类型子集和 dyn.a 动态标识，不代表完整 LaunchServices 或 UniformTypeIdentifiers。尚未处理应用自定义类型注册和全部 Apple 类型声明；macOS 对照也不等同于所有 iOS 版本行为。
