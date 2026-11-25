# OpenSSL

证书与信任链后端使用 OpenSSL 官方 3.5.9 发行版，许可证为 Apache-2.0，保存在 LICENSE.txt。

- 官方仓库：https://github.com/openssl/openssl
- 官方发行：https://github.com/openssl/openssl/releases/tag/openssl-3.5.9
- 发布日期：2026-09-29。
- 标签对象：`d0ca66a1abe52545f14eca635c648932fcde5615`。
- 标签解析后的提交：`45e844fa2a14ec92d146bd8f5778ac130b6625fb`。
- 源码包：`openssl-3.5.9.tar.gz`。
- 源码包 SHA-256：`603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a`，已与官方 Release API digest 核对。

`tools/build_crypto.sh` 在 GitHub Actions 下载并校验固定源码包，为 Linux aarch64 与 Android arm64 API 26 分别构建静态 libcrypto。源码未作本地修改。APK 将该库静态链接进 libchimera.so，隐藏第三方库导出符号。构建缓存按版本、平台及配置代号区分；二进制包含在持久化的 native artifact 与 APK Release 中。

本机无需下载 OpenSSL 源码或编译。Linux 构建目录为 crypto-native，Android 构建目录为 crypto-android。手动构建需先用相应平台运行构建脚本，并把 crypto-native/include 与 crypto-native/lib/libcrypto.a 加入宿主编译命令。