# 网络可达性兼容范围

AFNetworking 原始二进制调用六个 SCNetworkReachability 接口：按 sockaddr 或域名创建目标、读取 flags、注册回调，以及在主 RunLoop 的 common modes 上添加和移除监听。默认 manager 使用全零 IPv6 sockaddr。上下文 retain/release 调用 Block_copy/Block_release，必须保存 retain 的返回值并在监听替换或对象销毁时配对释放。

调用证据来自 PojavLauncher 2.2 所带 libAFNetworking.dylib，SHA-256 为 `2412354f7aa6659a84eb8d9a9f69e7dd6386dec2ed8f23b6979d8927c172b35b`。本地反汇编节选保存在工作区 `pojav-validation/references/afnetworking-reachability-call.txt`。

接口契约参考 Apple 官方 configd 仓库的 [SCNetworkReachability.h](https://github.com/apple-oss-distributions/configd/blob/main/SystemConfiguration.fproj/SCNetworkReachability.h)。可达性描述数据包能否离开本机，不保证服务在线。未调度域名查询按路由给出乐观结果，开始监听后异步 DNS 结果参与后续状态。

实现接收 Darwin ARM64 的 16 字节 IPv4 / 28 字节 IPv6 sockaddr，转换为宿主结构。UDP connect/getsockname 仅查询内核选路，不调用 send 或发送应用探测报文。全零地址检查 IPv4/IPv6 的文档网段目的地，域名解析使用系统 getaddrinfo。接口枚举补充本地、直连与已知蜂窝接口名标志。DNS 查询在现有 dispatch 后台线程运行，结果在主循环中交付；主循环运行时约每秒复查路由，域名结果约 30 秒刷新，临时解析错误约 5 秒重试。

监听支持主 RunLoop 的 default/common modes。回调上下文与对象分别计数，回调执行期间固定其上下文，允许回调中取消监听、替换回调或释放目标。会话销毁先停止并等待 dispatch 线程，再释放对象及上下文，防止访问已卸载的 guest 代码。域名解析使用系统超时，销毁时可能等待尚未返回的系统解析。

当前不支持其他线程的 RunLoop、自定义模式、地址对、dispatch queue 调度、NAT64 地址合成或拨号自动连接。系统不允许枚举接口时只能返回已查明的可达性，不推测蜂窝、直连标志。蜂窝识别依赖 rmnet/ccmni/wwan/pdp 等接口名，不声称完整复制 Network.framework 或 Android ConnectivityManager。文档网段选路检查不等于枚举所有默认路由；多路由、VPN 与特殊策略仍以宿主内核对该目标的选路为准。

Darwin 夹具覆盖地址布局、IPv4/IPv6 回环、全零地址、失败时输出参数保持不变、retain 返回值、替换上下文、延迟回调、取消与重入释放。宿主测试额外在隔离网络命名空间关闭和恢复 loopback，要求实际观察到三次状态通知。Android instrumentation 在实际应用 UID 下运行三轮同一夹具。构建与实测结论应以各提交的验证归档为准。
