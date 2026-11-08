#!/usr/bin/env bash
set -euo pipefail

[[ "$(uname -s)" == Darwin ]] || { echo 'This script requires macOS and Xcode command line tools' >&2; exit 1; }
prefix="${1:?Usage: build-macos-deps.sh /absolute/install/prefix}"
[[ "$prefix" = /* && "$prefix" != *' '* ]] || { echo 'Use an absolute prefix without spaces' >&2; exit 1; }
export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-12.0}"
arch="${PURR_ARCH:-$(uname -m)}"
case "$arch" in
    arm64) vpx_target=arm64-darwin20-gcc; openssl_target=darwin64-arm64-cc ;;
    x86_64) vpx_target=x86_64-darwin20-gcc; openssl_target=darwin64-x86_64-cc ;;
    *) echo "Unsupported architecture: $arch" >&2; exit 1 ;;
esac
jobs="$(sysctl -n hw.logicalcpu)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$prefix/lib/pkgconfig" "$prefix/licenses"
export PKG_CONFIG_PATH="$prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export CC="$(xcrun -f clang)"
export CXX="$(xcrun -f clang++)"
export CFLAGS="-arch $arch -mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET"
export CXXFLAGS="$CFLAGS"
export LDFLAGS="$CFLAGS"

download() {
    local url="$1" checksum="$2" target="$3"
    curl --fail --location --retry 3 --output "$target" "$url"
    local actual
    actual="$(shasum -a 256 "$target" | cut -d ' ' -f1)"
    [[ "$actual" == "$checksum" ]] || { echo "Checksum mismatch: $url" >&2; exit 1; }
}

download https://codeload.github.com/webmproject/libvpx/tar.gz/refs/tags/v1.15.2 \
    26fcd3db88045dee380e581862a6ef106f49b74b6396ee95c2993a260b4636aa "$work/libvpx.tar.gz"
tar -xzf "$work/libvpx.tar.gz" -C "$work"
(
    cd "$work/libvpx-1.15.2"
    ./configure --prefix="$prefix" --target="$vpx_target" --enable-shared --disable-static \
        --disable-examples --disable-tools --disable-docs --disable-unit-tests \
        --disable-vp8-encoder --disable-vp9-encoder
    make -j "$jobs"
    make install
    cp LICENSE "$prefix/licenses/libvpx-LICENSE"
)

download https://codeload.github.com/FFmpeg/FFmpeg/tar.gz/refs/tags/n6.1.4 \
    78dd367fbfc5d4a9949d528808334accffcc8e898aee30fa9fab6999c4751e3a "$work/ffmpeg.tar.gz"
tar -xzf "$work/ffmpeg.tar.gz" -C "$work"
(
    cd "$work/FFmpeg-n6.1.4"
    ./configure --prefix="$prefix" --arch="$arch" --target-os=darwin \
        --cc="$CC" --cxx="$CXX" --extra-cflags="$CFLAGS" --extra-ldflags="$LDFLAGS" \
        --enable-shared --disable-static --disable-autodetect --enable-libvpx \
        --disable-programs --disable-doc --disable-avdevice --disable-avfilter \
        --disable-postproc --disable-network --disable-encoders --disable-muxers \
        --disable-protocols --enable-protocol=file
    make -j "$jobs"
    make install
    cp COPYING.LGPLv2.1 "$prefix/licenses/FFmpeg-COPYING.LGPLv2.1"
    cp LICENSE.md "$prefix/licenses/FFmpeg-LICENSE.md"
)

download https://github.com/openssl/openssl/releases/download/openssl-3.5.7/openssl-3.5.7.tar.gz \
    a8c0d28a529ca480f9f36cf5792e2cd21984552a3c8e4aa11a24aa31aeac98e8 "$work/openssl.tar.gz"
tar -xzf "$work/openssl.tar.gz" -C "$work"
(
    cd "$work/openssl-3.5.7"
    ./Configure "$openssl_target" --prefix="$prefix" --libdir=lib shared no-tests no-apps
    make -j "$jobs"
    make install_sw
    cp LICENSE.txt "$prefix/licenses/OpenSSL-LICENSE.txt"
)
cat > "$prefix/licenses/SOURCES.txt" <<'EOF'
The bundled libraries are dynamically linked. Corresponding source and build
options are available in scripts/build-macos-deps.sh in the PurrPause source tree.

FFmpeg 6.1.4 (LGPL-2.1-or-later): https://github.com/FFmpeg/FFmpeg/tree/n6.1.4
libvpx 1.15.2 (BSD-3-Clause): https://github.com/webmproject/libvpx/tree/v1.15.2
OpenSSL 3.5.7 (Apache-2.0): https://github.com/openssl/openssl/tree/openssl-3.5.7
Qt 6.5.3 (LGPL-3.0): https://download.qt.io/archive/qt/6.5/6.5.3/submodules/
Qt license: https://www.gnu.org/licenses/lgpl-3.0.html
EOF
