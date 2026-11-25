# 证书与信任链兼容范围

本模块实现原始 PojavLauncher 2.2 的 AFNetworking 所用 11 项 Security C 接口：SecCertificateCreateWithData、SecCertificateCopyData、SecPolicyCreateBasicX509、SecPolicyCreateSSL、SecTrustCreateWithCertificates、SecTrustSetPolicies、SecTrustSetAnchorCertificates、SecTrustEvaluate、SecTrustGetCertificateCount、SecTrustGetCertificateAtIndex、SecTrustCopyPublicKey。

证书 DER 解析、签名验证、链构建、CA/用途/路径长度约束、RSA/EC 公钥解析由固定版本 OpenSSL 3.5.9 完成，来源与许可证见 third_party/openssl/ORIGIN.md。兼容层处理 CF/Objective-C 生命周期、策略输入、信任结果、域名检查、选定 Apple TLS 约束和错误参数。默认锚点使用宿主系统信任库；Android 优先读取 Conscrypt APEX 系统 CA，旧系统回退至 system CA 目录，Linux 使用系统 CA 文件。没有自动信任输入链附带的根证书。

设置自定义 anchor 数组后只信任这些锚点。空数组表示没有锚点，NULL 恢复系统锚点。允许显式信任中间证书。多项策略必须全部通过，修改策略或锚点使旧链结果失效。SecTrustEvaluate 成功执行返回 OSStatus 0，但证书可信与否另由结果表示：4 为通过，5 为未通过。未通过信任评估不妨碍读取叶证书公钥，符合 AFNetworking 的证书固定流程。

TLS server 检查 DNS/IP、完整标签通配符、EKU、SAN，以及 2019 年后的非系统信任证书 825 天加一小时上限、2020 年后的系统信任证书 398 天上限。阈值来自 Apple 官方 SecPolicy.c，扩展用例在 macOS 15.8.2 / SDK 15.5 对照验证。OpenSSL 认证强度设为 level 2，拒绝弱 RSA 等不满足 112-bit 强度要求的证书。Basic X509 不套用 TLS 有效期上限。

证书按编码内容比较，公钥按 SubjectPublicKeyInfo 内容比较。Objective-C 数组加入受管理对象后持有它，复制、插入、批量添加、删除和销毁配对引用，支持 AFNetworking 将证书／策略加入数组后立即释放自身引用的实际用法。CoreFoundation 数组仍遵守显式 retain/release callbacks，不重复保留。

测试使用独立生成的 RSA 2048、EC P-256 和专用弱 RSA 1024 公共证书，私钥不落盘。覆盖信任根、错误域名、SAN/CN、通配符、IP、过期、尚未生效、篡改签名、错误用途、缺中间证书、非 CA、路径长度、长有效期、空锚点、策略切换、链枚举、公钥比较和数组所有权。另以 https://letsencrypt.org/certs/isrgrootx1.der 官方证书验证系统信任库正向路径，SHA-256 为 96bcec06264976f37460779acf28c5a7cfe8a3c0aae11a8ffcee05c0bddf08c6。扩展夹具包含 35 次信任评估。不同任务生成不同密钥，不能仅因用例相同就宣称证书字节相同；本机还应使用下载的 Apple 原始证书重跑，验证同输入返回。

已知差异：Apple 的越界 SecTrustGetCertificateAtIndex 可抛异常，兼容层返回 NULL；Apple 相同公钥的 CFHash 实测不相等，兼容层提供一致内容哈希。这两项单独记录，不假称完全匹配。类型 ID 是兼容运行时内部标识，不保证和 Apple 数值一致。

当前未实现 AIA 自动下载、OCSP/CRL 在线吊销、Certificate Transparency、Apple trustd/keychain 的全部策略、用户信任覆盖、企业配置、IDNA 规范化和所有历史系统行为。最大证书大小 1 MiB、输入列表最多 256 项、验证链深度 32。仅对选定接口和用例验收，不宣称完整 Security.framework 或 Apple TLS 安全策略等价。

本批也未接入 NSURLSession 的完整认证 challenge 与证书回传。宿主网络传输继续使用原有平台 TLS 校验；这些接口通过不代表原始 AFNetworking 已实现完整网络请求，更不代表原始 Pojav 已能启动。后续仍需 NSURLCache、NSURLCredential 及更广的启动依赖。