#!/usr/bin/env bash
set -euo pipefail

[[ "$(uname -s)" == Darwin ]] || { echo 'DMG verification requires macOS' >&2; exit 1; }
dmg="${1:?Usage: verify-macos-package.sh package.dmg [evidence-directory]}"
arch="${PURR_ARCH:-$(uname -m)}"
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dmg="$(cd "$(dirname "$dmg")" && pwd)/$(basename "$dmg")"
evidence="${2:-dist/verification/macos-$arch}"
mkdir -p "$evidence"
evidence="$(cd "$evidence" && pwd)"
scratch="$(mktemp -d)"
mountpoint="$scratch/DMG mount"
mkdir -p "$mountpoint" "$scratch/Applications with spaces"
mounted=false
cleanup() {
    if [[ "$mounted" == true ]]; then
        hdiutil detach "$mountpoint" > "$evidence/detach.log" 2>&1 || \
            hdiutil detach -force "$mountpoint" >> "$evidence/detach.log" 2>&1 || true
    fi
    rm -rf "$scratch"
}
trap cleanup EXIT
hdiutil verify "$dmg" 2>&1 | tee "$evidence/dmg-integrity.log"
hdiutil attach "$dmg" -readonly -nobrowse -noautoopen -mountpoint "$mountpoint" -plist > "$evidence/mount.plist"
mounted=true
[[ -d "$mountpoint/purr-pause.app" ]] || { echo 'DMG does not contain purr-pause.app' >&2; exit 1; }
[[ -L "$mountpoint/Applications" && "$(readlink "$mountpoint/Applications")" == /Applications ]] || {
    echo 'DMG is missing the Applications installation shortcut' >&2; exit 1;
}
app="$scratch/Applications with spaces/Purr Pause.app"
ditto "$mountpoint/purr-pause.app" "$app"
hdiutil detach "$mountpoint" > "$evidence/detach.log" 2>&1
mounted=false

python3 - "$app" "$evidence/bundle-metadata.json" <<'PY'
import json, pathlib, plistlib, sys
app = pathlib.Path(sys.argv[1])
with (app / 'Contents/Info.plist').open('rb') as file:
    metadata = plistlib.load(file)
expected = {'CFBundleExecutable': 'purr-pause', 'CFBundleIdentifier': 'com.purr-pause.app',
            'LSMinimumSystemVersion': '12.0', 'LSUIElement': True}
for key, value in expected.items():
    if metadata.get(key) != value:
        raise SystemExit(f'Invalid {key}: {metadata.get(key)!r}, expected {value!r}')
required = ['Contents/PlugIns/platforms/libqcocoa.dylib',
            'Contents/PlugIns/tls/libqsecuretransportbackend.dylib',
            'Contents/Resources/assets/webm/cat-walk.webm',
            'Contents/Resources/assets/webm/cat-rest.webm',
            'Contents/Resources/assets/images/logo.png', 'Contents/Resources/icon.icns',
            'Contents/Resources/docs/message-api-spec.md', 'Contents/Resources/LICENSE']
for name in required:
    path = app / name
    if not path.is_file() or path.stat().st_size == 0:
        raise SystemExit(f'Missing or empty bundle resource: {name}')
pathlib.Path(sys.argv[2]).write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + '\n')
PY
codesign --verify --deep --strict --verbose=2 "$app" 2>&1 | tee "$evidence/codesign-installed.log"
codesign --display --verbose=4 "$app" > "$evidence/signature.txt" 2>&1
if [[ "${MACOS_SIGN_IDENTITY:--}" != - ]]; then
    grep -F 'Authority=Developer ID Application:' "$evidence/signature.txt" >/dev/null || {
        echo 'Expected a Developer ID signature but the installed application has none' >&2; exit 1;
    }
    codesign --verify --strict --verbose=2 "$dmg" 2>&1 | tee "$evidence/codesign-dmg.log"
fi
python3 "$project_dir/scripts/bundle-macos-libs.py" "$app" --arch "$arch" --verify-only \
    2>&1 | tee "$evidence/installed-dependencies.log"

# A relocated app must launch using Cocoa and only its own deployed libraries.
# --verify-install fails on missing TLS, resources, decoded frames or WebM alpha.
python3 - "$app" "$scratch" "$evidence" "$arch" <<'PY'
import json, os, pathlib, subprocess, sys
app, scratch, evidence = (pathlib.Path(value).resolve() for value in sys.argv[1:4])
arch = sys.argv[4]
environment = dict(os.environ)
for key in ('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QML2_IMPORT_PATH',
            'DYLD_LIBRARY_PATH', 'DYLD_FRAMEWORK_PATH', 'DYLD_FALLBACK_LIBRARY_PATH',
            'DYLD_FALLBACK_FRAMEWORK_PATH'):
    environment.pop(key, None)
environment['QT_QPA_PLATFORM'] = 'cocoa'
command = [str(app / 'Contents/MacOS/purr-pause'), '--verify-install',
           '--verification-report', str(evidence / 'installed-cocoa.json'),
           '--data-dir', str(scratch / 'Temporary user data')]
with (evidence / 'installed-cocoa.log').open('w') as log:
    try:
        result = subprocess.run(command, cwd=scratch, env=environment, stdout=log,
                                stderr=subprocess.STDOUT, timeout=45, check=False)
    except subprocess.TimeoutExpired as error:
        raise SystemExit('Installed Cocoa verification timed out; inspect installed-cocoa.log') from error
if result.returncode:
    raise SystemExit(f'Installed Cocoa verification exited {result.returncode}; inspect installed-cocoa.log')
if not (evidence / 'installed-cocoa.json').is_file():
    raise SystemExit('Installed Cocoa verification did not write its report')
report = json.loads((evidence / 'installed-cocoa.json').read_text())
if report.get('ok') is not True or report.get('platform') != 'cocoa':
    raise SystemExit('Installed application did not report successful Cocoa verification')
if report.get('architecture') != arch or not report.get('tlsBackend'):
    raise SystemExit('Installed application reported the wrong architecture or no TLS backend')
videos = report.get('videos', [])
if len(videos) != 2 or {video.get('file') for video in videos} != {'cat-walk.webm', 'cat-rest.webm'}:
    raise SystemExit('Installed application did not verify both original cat videos')
if any(video.get('transparent') is not True or video.get('opaque') is not True
       or video.get('width', 0) <= 0 or video.get('height', 0) <= 0 for video in videos):
    raise SystemExit('Installed application did not verify video dimensions and transparent/opaque pixels')
for key in ('executable', 'assets', 'docs'):
    path = pathlib.Path(report.get(key, '')).resolve()
    if app not in path.parents:
        raise SystemExit(f'Installed verification used a resource outside the relocated app: {key}')
PY
if [[ -n "${MACOS_NOTARY_PROFILE:-}" ]]; then
    xcrun stapler validate "$dmg" 2>&1 | tee "$evidence/staple-validation.log"
    spctl --assess --type open --context context:primary-signature --verbose=2 "$dmg" \
        2>&1 | tee "$evidence/gatekeeper-dmg.log"
fi
echo "Verified installed DMG: $arch, Cocoa, TLS, alpha video, resources, signatures and relocation"
