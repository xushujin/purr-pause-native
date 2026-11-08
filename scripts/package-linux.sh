#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build}"
output_dir="${2:-dist}"
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$(cd "$build_dir" && pwd)"
mkdir -p "$output_dir"
output_dir="$(cd "$output_dir" && pwd)"
version="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$build_dir/CMakeCache.txt")"
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo 'CMake project version is missing or invalid' >&2; exit 1; }
arch="$(dpkg --print-architecture)"
case "$arch" in amd64|arm64) ;; *) echo "Unsupported architecture: $arch" >&2; exit 1 ;; esac
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
DESTDIR="$stage" cmake --install "$build_dir" --prefix /usr
mkdir -p "$stage/DEBIAN"

# Derive exact ABI package dependencies on the build distribution. In release CI
# this runs on Ubuntu 22.04, so a newer build host cannot silently raise the floor.
mkdir -p "$stage/debian"
cat > "$stage/debian/control" <<'EOF'
Source: purr-pause
Section: utils
Priority: optional
Maintainer: PurrPause Contributors <purr-pause@example.com>
Standards-Version: 4.6.0

Package: purr-pause
Architecture: any
Description: Native desktop cat break reminder
EOF
shlib_args=(-O "-e$stage/usr/bin/purr-pause" -xpurr-pause "-S$stage")
if [[ -d "$stage/usr/lib/purr-pause" ]]; then
    shlib_args+=("-l$stage/usr/lib/purr-pause")
    find "$stage/usr/lib/purr-pause" -type f -name '*.so*' -print0 > "$stage/debian/bundled-libraries"
    while IFS= read -r -d '' library; do
        soname="$(objdump -p "$library" | sed -n 's/^[[:space:]]*SONAME[[:space:]]*//p')"
        [[ "$soname" =~ ^(lib[^/]+)\.so\.([0-9]+)$ ]] || {
            echo "Missing or unsupported SONAME: $library: $soname" >&2
            exit 1
        }
        printf '%s %s purr-pause\n' "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}" >> "$stage/debian/shlibs.local"
        shlib_args+=("-e$library")
    done < "$stage/debian/bundled-libraries"
fi
dependencies="$(cd "$stage" && dpkg-shlibdeps "${shlib_args[@]}" | sed -n 's/^shlibs:Depends=//p')"
[[ -n "$dependencies" ]] || { echo 'Could not determine runtime dependencies' >&2; exit 1; }
rm -rf "$stage/debian"
cat > "$stage/DEBIAN/control" <<EOF
Package: purr-pause
Version: $version
Section: utils
Priority: optional
Architecture: $arch
Maintainer: PurrPause Contributors <purr-pause@example.com>
Depends: $dependencies, qt6-qpa-plugins, libqt6svg6
Recommends: xwayland, fonts-noto-cjk
Installed-Size: $(du -sk "$stage/usr" | cut -f1)
Description: PurrPause native desktop break reminder
 A native Qt desktop cat that reminds you to rest, with local settings,
 transparent animations and message notifications.
EOF
cp "$project_dir/packaging/postinst" "$stage/DEBIAN/postinst"
cp "$project_dir/packaging/postrm" "$stage/DEBIAN/postrm"
chmod 0755 "$stage/DEBIAN/postinst" "$stage/DEBIAN/postrm"
dpkg-deb --root-owner-group --build "$stage" "$output_dir/purr-pause_${version}_${arch}.deb"
