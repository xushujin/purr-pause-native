#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build}"
output_dir="${2:-dist}"
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$(cd "$build_dir" && pwd)"
mkdir -p "$output_dir"
output_dir="$(cd "$output_dir" && pwd)"
for tool in cmake python3 qmake6 patchelf mksquashfs curl sha256sum; do
    command -v "$tool" >/dev/null || { echo "Required packaging tool missing: $tool" >&2; exit 1; }
done
version="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$build_dir/CMakeCache.txt")"
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo 'Missing CMake project version' >&2; exit 1; }
case "$(uname -m)" in
    x86_64) arch=x86_64 ;;
    aarch64|arm64) arch=aarch64 ;;
    *) echo 'AppImage supports native x86_64 and aarch64 builds' >&2; exit 1 ;;
esac
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
appdir="$work/PurrPause.AppDir"
DESTDIR="$appdir" cmake --install "$build_dir" --prefix /usr
[[ -d "$appdir/usr/lib/purr-pause" ]] || {
    echo 'Use the portable codec build with -DPURR_CODEC_PREFIX before packaging AppImage' >&2
    exit 1
}
cp "$project_dir/packaging/AppRun" "$appdir/AppRun"
chmod 0755 "$appdir/AppRun"
cp "$project_dir/packaging/purr-pause.desktop" "$appdir/purr-pause.desktop"
cp "$project_dir/assets/images/logo.png" "$appdir/purr-pause.png"
ln -s purr-pause.png "$appdir/.DirIcon"
cat > "$appdir/usr/bin/qt.conf" <<'EOF'
[Paths]
Prefix=..
Libraries=lib
Plugins=plugins
EOF
python3 "$project_dir/scripts/bundle-appimage-libs.py" "$appdir" \
    --qt-plugins "$(qmake6 -query QT_INSTALL_PLUGINS)"
mkdir -p "$appdir/usr/etc/fonts" "$appdir/usr/share/fonts" "$appdir/usr/share/doc/purr-pause/appimage-libraries"
font=/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc
[[ -r "$font" ]] || { echo 'Install fonts-noto-cjk to package Chinese text support' >&2; exit 1; }
cp "$font" "$appdir/usr/share/fonts/"
cp "$project_dir/packaging/appimage-fonts.conf" "$appdir/usr/etc/fonts/fonts.conf"
cp /etc/ssl/certs/ca-certificates.crt "$appdir/usr/share/purr-pause/ca-certificates.crt"
for package in fonts-noto-cjk ca-certificates; do
    cp "/usr/share/doc/$package/copyright" "$appdir/usr/share/doc/purr-pause/appimage-libraries/$package-copyright"
done

# Runtime revision and archive hashes are kept together in a reviewable manifest.
# No latest/continuous asset is trusted without its fixed SHA256.
runtime_manifest="$project_dir/packaging/appimage-runtime.tsv"
runtime_url="$(awk -F '\t' -v arch="$arch" '$1 == arch { print $2 }' "$runtime_manifest")"
runtime_sha="$(awk -F '\t' -v arch="$arch" '$1 == arch { print $3 }' "$runtime_manifest")"
[[ "$runtime_sha" =~ ^[a-f0-9]{64}$ && "$runtime_url" == https://* ]] || {
    echo "Missing pinned AppImage runtime for $arch" >&2
    exit 1
}
curl --fail --location --retry 2 --connect-timeout 15 --max-time 120 \
    --output "$work/runtime" "$runtime_url"
actual_sha="$(sha256sum "$work/runtime" | cut -d ' ' -f1)"
[[ "$actual_sha" == "$runtime_sha" ]] || { echo 'AppImage runtime checksum mismatch' >&2; exit 1; }
cp "$runtime_manifest" "$appdir/usr/share/doc/purr-pause/appimage-libraries/runtime.tsv"
cp "$project_dir/packaging/appimage-runtime-LICENSE" "$appdir/usr/share/doc/purr-pause/appimage-libraries/runtime-LICENSE"
cat > "$appdir/usr/share/doc/purr-pause/appimage-libraries/SOURCES.txt" <<'EOF'
PurrPause bundles dynamically linked Ubuntu 22.04 libraries, Qt 6 plugins,
OpenSSL 3, Noto CJK fonts and a fallback CA store. The individual copyright
notices and exact package/library manifest are included in this directory.
Ubuntu source packages: https://launchpad.net/ubuntu/jammy/+source/PACKAGE
Matching sources can be downloaded using apt-get source PACKAGE on Ubuntu 22.04.
Qt source and LGPL terms: https://download.qt.io/archive/qt/6.2/6.2.4/submodules/
FFmpeg and libvpx source/build details: ../third-party/SOURCES.txt
The libraries remain independently replaceable after --appimage-extract.
AppImage type2 runtime source: https://github.com/AppImage/type2-runtime
The fixed runtime revision and SHA256 are recorded in runtime.tsv.
EOF
mksquashfs "$appdir" "$work/payload.squashfs" -noappend -all-root -no-xattrs \
    -comp zstd -processors "${PURR_BUILD_JOBS:-2}" -root-mode 0755
artifact="$output_dir/purr-pause-${version}-${arch}.AppImage"
cat "$work/runtime" "$work/payload.squashfs" > "$artifact"
chmod 0755 "$artifact"
python3 - "$artifact" <<'PY'
import pathlib, sys
with pathlib.Path(sys.argv[1]).open('rb') as stream:
    header = stream.read(16)
if header[:4] != b'\x7fELF' or header[8:11] != b'AI\x02':
    raise SystemExit('Invalid AppImage type 2 runtime header')
PY
if [[ -n "${PURR_APPIMAGE_KEEP_APPDIR:-}" ]]; then
    [[ ! -e "$PURR_APPIMAGE_KEEP_APPDIR" ]] || { echo 'AppDir destination already exists' >&2; exit 1; }
    cp -a "$appdir" "$PURR_APPIMAGE_KEEP_APPDIR"
fi
echo "Created $artifact"
