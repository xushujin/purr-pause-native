#!/usr/bin/env bash
set -euo pipefail

[[ "$(uname -s)" == Linux ]] || { echo 'This script requires Linux' >&2; exit 1; }
codec_prefix="${1:?Usage: build-linux-codecs.sh /absolute/install/prefix}"
[[ "$codec_prefix" = /* && "$codec_prefix" != *[[:space:]]* ]] || {
    echo 'Use an absolute prefix without whitespace' >&2; exit 1;
}
for codec_tool in curl tar make pkg-config patchelf gcc g++; do
    command -v "$codec_tool" >/dev/null || { echo "Missing build tool: $codec_tool" >&2; exit 1; }
done
case "$(uname -m)" in
    x86_64)
        codec_arch=x86_64
        vpx_target=x86_64-linux-gcc
        command -v nasm >/dev/null || { echo 'Install nasm for x86_64 assembly' >&2; exit 1; }
        ;;
    aarch64|arm64)
        codec_arch=aarch64
        vpx_target=arm64-linux-gcc
        ;;
    *) echo 'Supported architectures: x86_64 and aarch64' >&2; exit 1 ;;
esac
codec_jobs="${PURR_BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN)}"
[[ "$codec_jobs" =~ ^[1-9][0-9]*$ ]] || { echo 'PURR_BUILD_JOBS must be a positive integer' >&2; exit 1; }
codec_work="$(mktemp -d)"
trap 'rm -rf "$codec_work"' EXIT
mkdir -p "$codec_prefix/lib/pkgconfig" "$codec_prefix/licenses"
export PKG_CONFIG_PATH="$codec_prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

download() {
    local source_url="$1" expected_sha256="$2" archive_path="$3" actual_sha256
    curl --fail --location --retry 3 --output "$archive_path" "$source_url"
    actual_sha256="$(sha256sum "$archive_path" | cut -d ' ' -f1)"
    [[ "$actual_sha256" == "$expected_sha256" ]] || {
        echo "Checksum mismatch: $source_url" >&2; exit 1;
    }
}

# Build on Ubuntu 22.04: these private shared libraries then avoid depending on
# distribution-specific FFmpeg SONAME packages removed in newer Ubuntu releases.
download https://codeload.github.com/webmproject/libvpx/tar.gz/refs/tags/v1.15.2 \
    26fcd3db88045dee380e581862a6ef106f49b74b6396ee95c2993a260b4636aa "$codec_work/libvpx.tar.gz"
tar -xzf "$codec_work/libvpx.tar.gz" -C "$codec_work"
(
    cd "$codec_work/libvpx-1.15.2"
    ./configure --prefix="$codec_prefix" --libdir="$codec_prefix/lib" --target="$vpx_target" \
        --enable-shared --disable-static --enable-pic \
        --disable-examples --disable-tools --disable-docs --disable-unit-tests \
        --disable-vp8-encoder --disable-vp9-encoder \
        --enable-vp8-decoder --enable-vp9-decoder
    make -j "$codec_jobs"
    make install
    cp LICENSE "$codec_prefix/licenses/libvpx-LICENSE"
)

download https://codeload.github.com/FFmpeg/FFmpeg/tar.gz/refs/tags/n6.1.4 \
    78dd367fbfc5d4a9949d528808334accffcc8e898aee30fa9fab6999c4751e3a "$codec_work/ffmpeg.tar.gz"
tar -xzf "$codec_work/ffmpeg.tar.gz" -C "$codec_work"
(
    cd "$codec_work/FFmpeg-n6.1.4"
    # This upstream executable has no shebang. Native shells recover from
    # ENOEXEC, but QEMU/proot cannot; an explicit interpreter also makes the
    # version/SONAME generation work in emulated ARM validation builds.
    sed -i '1i#!/bin/sh' ffbuild/libversion.sh
    ./configure --prefix="$codec_prefix" --libdir="$codec_prefix/lib" \
        --arch="$codec_arch" --target-os=linux \
        --enable-shared --disable-static --enable-pic \
        --disable-autodetect --disable-everything \
        --disable-programs --disable-doc --disable-network \
        --disable-avdevice --disable-avfilter --disable-postproc --disable-swresample \
        --enable-avformat --enable-avcodec --enable-avutil --enable-swscale \
        --enable-libvpx --enable-decoder=libvpx_vp8,libvpx_vp9 \
        --enable-demuxer=matroska --enable-parser=vp8,vp9 --enable-protocol=file \
        --extra-cflags="-I$codec_prefix/include" --extra-ldflags="-L$codec_prefix/lib"
    make -j "$codec_jobs"
    make install
    cp COPYING.LGPLv2.1 "$codec_prefix/licenses/FFmpeg-COPYING.LGPLv2.1"
    cp LICENSE.md "$codec_prefix/licenses/FFmpeg-LICENSE.md"
)

# Set RUNPATH after linking to avoid shell/make expansion of the literal $ORIGIN.
# Patch each real file once. make install retains the standard .so symlink chain.
find "$codec_prefix/lib" -maxdepth 1 -type f -name '*.so*' -print0 > "$codec_work/libraries.list"
while IFS= read -r -d '' codec_library; do
    patchelf --set-rpath '$ORIGIN' "$codec_library"
done < "$codec_work/libraries.list"

for codec_soname in libvpx.so.11 libavcodec.so.60 libavformat.so.60 libavutil.so.58 libswscale.so.7; do
    [[ -f "$codec_prefix/lib/$codec_soname" ]] || {
        echo "Missing expected shared library: $codec_soname" >&2; exit 1;
    }
    [[ "$(patchelf --print-soname "$codec_prefix/lib/$codec_soname")" == "$codec_soname" ]] || {
        echo "Unexpected SONAME: $codec_soname" >&2; exit 1;
    }
    [[ "$(patchelf --print-rpath "$codec_prefix/lib/$codec_soname")" == '$ORIGIN' ]] || {
        echo "Invalid private-library RUNPATH: $codec_soname" >&2; exit 1;
    }
done

cat > "$codec_prefix/licenses/SOURCES.txt" <<'EOF'
PurrPause privately bundles these dynamically linked libraries. System Qt and
OpenSSL are not included here. Build options and SHA256-verified downloads are
recorded in scripts/build-linux-codecs.sh in the corresponding PurrPause source.

FFmpeg 6.1.4 (LGPL-2.1-or-later):
https://github.com/FFmpeg/FFmpeg/tree/n6.1.4
https://codeload.github.com/FFmpeg/FFmpeg/tar.gz/refs/tags/n6.1.4
SHA256: 78dd367fbfc5d4a9949d528808334accffcc8e898aee30fa9fab6999c4751e3a
Only Matroska/WebM demuxing, libvpx VP8/VP9 decoders, VP8/VP9 parsers, local file
access and pixel conversion are enabled. Alpha support remains enabled.
No GPL or nonfree components, network, encoders or FFmpeg programs are enabled.

libvpx 1.15.2 (BSD-3-Clause):
https://github.com/webmproject/libvpx/tree/v1.15.2
https://codeload.github.com/webmproject/libvpx/tar.gz/refs/tags/v1.15.2
SHA256: 26fcd3db88045dee380e581862a6ef106f49b74b6396ee95c2993a260b4636aa
VP8 and VP9 decoders are enabled; encoders and standalone tools are disabled.

The .so libraries retain their standard SONAMEs and are independently replaceable
with ABI-compatible builds. Their RUNPATH is $ORIGIN for private dependencies.
EOF
echo "Linux WebM shared libraries are ready in $codec_prefix/lib"
