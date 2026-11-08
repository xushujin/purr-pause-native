#!/usr/bin/env bash
set -euo pipefail

[[ "$(uname -s)" == Darwin ]] || { echo 'This script requires macOS' >&2; exit 1; }
app="${1:-build/purr-pause.app}"
output_dir="${2:-dist}"
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
app="$(cd "$(dirname "$app")" && pwd)/$(basename "$app")"
[[ -x "$app/Contents/MacOS/purr-pause" ]] || { echo "Missing application: $app" >&2; exit 1; }
mkdir -p "$output_dir"
output_dir="$(cd "$output_dir" && pwd)"
arch="${PURR_ARCH:-$(uname -m)}"
version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$app/Contents/Info.plist")"
identity="${MACOS_SIGN_IDENTITY:--}"
deps="${PURR_DEPS_PREFIX:-}"
if [[ -n "${MACOS_NOTARY_PROFILE:-}" && "$identity" == - ]]; then
    echo 'MACOS_NOTARY_PROFILE requires MACOS_SIGN_IDENTITY with a Developer ID certificate' >&2
    exit 1
fi
if [[ "$identity" != - ]]; then
    security find-identity -v -p codesigning | grep -F -- "$identity" >/dev/null || {
        echo 'MACOS_SIGN_IDENTITY is not present as a valid code-signing identity in the keychain' >&2
        exit 1
    }
fi
evidence="$output_dir/verification/macos-$arch"
mkdir -p "$evidence"
deploy_args=("$app" -always-overwrite -no-strip -verbose=2)
bundle_args=("$app" --arch "$arch")
if [[ -n "$deps" ]]; then
    deploy_args+=("-libpath=$deps/lib")
    bundle_args+=(--search "$deps/lib")
    mkdir -p "$app/Contents/Resources/licenses"
    cp -R "$deps/licenses/." "$app/Contents/Resources/licenses/"
fi
if [[ -n "${QT_ROOT_DIR:-}" ]]; then
    bundle_args+=(--search "$QT_ROOT_DIR/lib")
    if [[ -f "$QT_ROOT_DIR/plugins/tls/libqsecuretransportbackend.dylib" ]]; then
        mkdir -p "$app/Contents/PlugIns/tls"
        cp "$QT_ROOT_DIR/plugins/tls/libqsecuretransportbackend.dylib" "$app/Contents/PlugIns/tls/"
    fi
    if [[ -f "$QT_ROOT_DIR/plugins/platforms/libqoffscreen.dylib" ]]; then
        mkdir -p "$app/Contents/PlugIns/platforms"
        cp "$QT_ROOT_DIR/plugins/platforms/libqoffscreen.dylib" "$app/Contents/PlugIns/platforms/"
    fi
    if [[ -d "$QT_ROOT_DIR/LICENSES" ]]; then
        mkdir -p "$app/Contents/Resources/licenses/Qt"
        cp -R "$QT_ROOT_DIR/LICENSES/." "$app/Contents/Resources/licenses/Qt/"
    fi
fi
cp "$project_dir/LICENSE" "$app/Contents/Resources/LICENSE"
macdeployqt "${deploy_args[@]}" 2>&1 | tee "$evidence/macdeployqt.log"
[[ -f "$app/Contents/PlugIns/tls/libqsecuretransportbackend.dylib" ]] || {
    echo 'Missing Qt Secure Transport TLS backend; HTTPS would not work' >&2
    exit 1
}
python3 "$project_dir/scripts/bundle-macos-libs.py" "${bundle_args[@]}" 2>&1 | tee "$evidence/bundle.log"

# Sign nested Mach-O code before the enclosing bundle. Ad hoc signatures also
# allow arm64 code modified by install_name_tool to run locally.
sign_args=(--force --sign "$identity")
if [[ "$identity" != - ]]; then sign_args+=(--options runtime --timestamp); fi
while IFS= read -r -d '' binary; do
    if file -b "$binary" | grep -q 'Mach-O'; then
        codesign "${sign_args[@]}" "$binary"
    fi
done < <(find "$app/Contents" -type f -print0)
while IFS= read -r framework; do
    codesign "${sign_args[@]}" "$framework"
done < <(find "$app/Contents/Frameworks" -depth -type d -name '*.framework')
codesign "${sign_args[@]}" --entitlements "$project_dir/packaging/entitlements.plist" "$app"
codesign --verify --deep --strict --verbose=2 "$app" 2>&1 | tee "$evidence/codesign-build.log"

staging="$(mktemp -d)"
trap 'rm -rf "$staging"' EXIT
ditto "$app" "$staging/purr-pause.app"
ln -s /Applications "$staging/Applications"
dmg="$output_dir/purr-pause-${version}-macos-${arch}.dmg"
hdiutil create -volname PurrPause -srcfolder "$staging" -ov -format UDZO "$dmg"
if [[ "$identity" != - ]]; then
    codesign --force --sign "$identity" --timestamp "$dmg"
fi
if [[ -n "${MACOS_NOTARY_PROFILE:-}" ]]; then
    notary_args=(--keychain-profile "$MACOS_NOTARY_PROFILE")
    if [[ -n "${MACOS_NOTARY_KEYCHAIN:-}" ]]; then
        notary_args+=(--keychain "$MACOS_NOTARY_KEYCHAIN")
    fi
    xcrun notarytool submit "$dmg" "${notary_args[@]}" --wait --timeout 30m \
        --output-format json > "$evidence/notarization.json"
    python3 - "$evidence/notarization.json" <<'PY'
import json, pathlib, sys
result = json.loads(pathlib.Path(sys.argv[1]).read_text())
if result.get('status') != 'Accepted':
    raise SystemExit('Apple notarization did not report Accepted; inspect notarization.json')
PY
    xcrun stapler staple "$dmg" 2>&1 | tee "$evidence/staple.log"
fi
"$project_dir/scripts/verify-macos-package.sh" "$dmg" "$evidence"
echo "Created $dmg"
