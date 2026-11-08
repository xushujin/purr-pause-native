# Repeatable visual captures

These tools capture the original five renderer pages and the native Qt Widgets using the same fixture. They validate input data, viewport dimensions, selected visible field values, and record actual font/geometry properties. They produce raw pixel differences; a successful capture does not mean pixel parity.

The complete legacy `purr-pause/` project has been removed with explicit approval. Only the minimal, unchanged differential reference files remain under `tests/reference/{behavior,lib,tools,renderer}`. The capture tool loads the original pages from `tests/reference/renderer/`. These references are test inputs, not part of the native product build or installation packages. Removing the legacy project does not imply that outstanding macOS execution, physical desktop acceptance, or pixel parity has passed.

Build `test_visual` with `BUILD_TESTING=ON`. It is a capture executable, separate from the usual Qt tests. On Ubuntu, install `python3-pil`, `xvfb`, `libnss3`, `libnspr4`, `libatk-bridge2.0-0`, `libasound2`, `fonts-dejavu-core`, `fonts-droid-fallback`, and `fonts-noto-cjk`. Qt and Electron must have access to the same installed fonts. Dependencies may use a `t64` package suffix on newer distributions.

Obtain the exact reference engine outside the repository, for example:

```bash
npm install --prefix /tmp/purr-visual-electron --no-audit --no-fund electron@33.2.1
```

From the repository root:

```bash
cmake --build build --target test_visual
TZ=Asia/Shanghai QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1 \
  build/test_visual --config tests/visual/fixture.json --output build/visual
TZ=Asia/Shanghai xvfb-run -a \
  /tmp/purr-visual-electron/node_modules/electron/dist/electron --no-sandbox \
  scripts/capture-visual-reference.cjs --config tests/visual/fixture.json --output build/visual
python3 scripts/compare-visual.py --config tests/visual/fixture.json \
  --captures build/visual --output build/visual/report
python3 -m unittest discover -s tests/visual -p 'test_*.py'
```

Use an output directory outside `docs/` for CI. Upload all of `build/visual/`, including both manifests. The fixture pins Electron 33.2.1, matching the source project. The tool rejects another Electron version and records the actual Chromium and Qt versions, operating systems, architecture, device pixel ratio and fixture SHA-256. The shared fixture contains only test data and public example URLs. The reference renderer blocks HTTP(S) requests and uses a mock preload with context isolation enabled; it does not launch the original application's main process or load its user settings.

The comparison exits unsuccessfully for missing pages or fields, duplicate page IDs, a changed fixture hash, mismatched versions/timezones, unexpected input values, or incorrect output dimensions. Geometry and font metrics are always reported. Exact pixel identity is an independent boolean: a nonzero pixel difference is never converted into a parity success percentage. No pixel masks, blur, registration, or tolerance thresholds are applied to the image comparison.

The viewport dimensions are explicit test cases, not assertions about initial window dimensions. The fixture's five views cover the ordinary settings, source settings, list, trial activation, and rules states. Interactive error, save, source addition/removal, keyboard and filter behavior remains covered by `test_dialogs` and `test_ui`.
