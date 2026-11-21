# Android 固定签名

GitHub Actions 的 APK 构建使用同一份 Chimera PKCS12 密钥。Debug 和 Release 使用相同证书；版本号取 APK workflow 的运行编号。

证书 SHA-256：

```text
bef9cd7f74ed6c5140ca94e3cb895878a931d3f246e1a07fc285f5cbbcd418ca
```

仓库 Actions Secrets：

- `CHIMERA_SIGNING_STORE`：PKCS12 文件的 Base64。
- `CHIMERA_SIGNING_PASSWORD`：密钥库及私钥密码。
- `CHIMERA_SIGNING_SHA256`：期望的证书 SHA-256。

私钥别名为 `chimera`。工作区私有备份在 `private/signing/`，该目录由 Git 忽略，不属于源码。请保存这份备份；不要重新生成密钥替代现有密钥。

Gradle 从 `CHIMERA_KEYSTORE_PATH` 和 `CHIMERA_KEYSTORE_PASSWORD` 读取配置。缺少配置时构建失败，不回退到自动生成的 Android 调试证书。CI 在上传前使用 apksigner 校验证书；临时密钥在任务结束时删除。

APK、提交号、证书信息和 SHA256SUMS 保存到 `chimera-debug-apk` artifact，同时发布到 `apk-<run_id>` 预发布 Release。

旧版本使用不同的临时调试签名，首次切换需要卸载旧版本并重新导入 IPA。固定签名版本之间可以直接覆盖安装。
