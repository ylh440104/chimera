target=${1:?target required}
prefix=${2:?installation directory required}
version=3.5.9
digest=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
if [ -f "$prefix/lib/libcrypto.a" ] && [ -f "$prefix/include/openssl/x509.h" ]; then
    exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
curl --fail --location --retry 4 --retry-all-errors "https://github.com/openssl/openssl/releases/download/openssl-$version/openssl-$version.tar.gz" -o "$work/source.tar.gz" || exit 1
printf '%s  %s\n' "$digest" "$work/source.tar.gz" | sha256sum -c - || exit 1
tar -xzf "$work/source.tar.gz" -C "$work" || exit 1
cd "$work/openssl-$version" || exit 1
if [ "$target" = android-arm64 ]; then
    export ANDROID_NDK_ROOT=${ANDROID_NDK_ROOT:?Android NDK required}
    export PATH="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH"
    perl Configure android-arm64 -D__ANDROID_API__=26 no-shared no-tests no-apps no-module no-legacy no-autoload-config -fPIC --prefix="$prefix" --libdir=lib || exit 1
else
    perl Configure "$target" no-shared no-tests no-apps no-module no-legacy no-autoload-config -fPIC --prefix="$prefix" --libdir=lib || exit 1
fi
make -j2 build_libs || exit 1
make install_dev || exit 1