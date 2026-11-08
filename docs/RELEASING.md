# 原生版本构建与发布

根目录的 `.github/workflows/native.yml` 是原生版本唯一的发布流程。完整旧 Electron 工程 `purr-pause/` 已按本次明确批准移除；`tests/reference/{behavior,lib,tools,renderer}` 仅保留原样的最小差分参考，供开发和自动测试使用，不参与原生产品构建，也不进入 deb、AppImage 或 DMG。

## GitHub Flow

1. 从 `main`（或 `master`）创建功能分支，提交修改并发起 Pull Request。
2. PR 自动执行四个构建任务：Ubuntu 22.04 amd64、Ubuntu 22.04 arm64、macOS Intel、macOS Apple Silicon。每个任务运行 CTest 并生成安装包，Linux 包还会在 Ubuntu 24.04、26.04 的两种架构上执行安装验收；macOS 会从生成的 DMG 重新安装并验证。构建产物和 `native-evidence-*` 验证报告在 Actions 页面分别下载。
3. 审查通过后合并。建议把 `Native build and release` 的构建和 Linux 安装兼容性任务设置为分支保护的必需检查。
4. 修改根 `CMakeLists.txt` 的 `project(... VERSION ...)` 版本号，完成平台验收后合并。推送完全匹配的版本标签，例如 `v1.5.0`，构建及兼容性测试都成功后，发布任务自动创建 GitHub Release 并上传安装包、源代码归档和 SHA256SUMS。

```sh
git tag -a v1.5.0 -m 'PurrPause native 1.5.0'
git push origin v1.5.0
```

推送标签会公开发布 release；普通 PR、分支提交和手动 workflow_dispatch 仅构建。重新运行同一标签会替换该 release 中的同名文件。仓库必须启用 Actions，标签发布任务需要允许 `GITHUB_TOKEN` 的 `contents: write` 权限。ARM Linux runner 的可用性取决于仓库类型和 GitHub 账户；不具备该 runner 时可将矩阵的 runner 标签改为配置了 Ubuntu 22.04 arm64 的自托管 runner。

## 输出及兼容性边界

| 产物 | 架构 | 最低目标系统 |
| --- | --- | --- |
| `purr-pause_1.5.0_amd64.deb` | x86_64 | Ubuntu 22.04 |
| `purr-pause_1.5.0_arm64.deb` | aarch64 | Ubuntu 22.04 |
| `purr-pause-1.5.0-x86_64.AppImage` | x86_64 | Ubuntu 22.04 |
| `purr-pause-1.5.0-aarch64.AppImage` | aarch64 | Ubuntu 22.04 |
| `purr-pause-1.5.0-macos-x86_64.dmg` | Intel | macOS 12.0 |
| `purr-pause-1.5.0-macos-arm64.dmg` | Apple Silicon | macOS 12.0 |

Linux 必须在 Ubuntu 22.04 构建发布包：在更新发行版编译会提高 glibc、Qt 的动态库要求。发布包把固定版本 FFmpeg 6.1.4、libvpx 1.15.2 动态库安装到 `/usr/lib/purr-pause`，通过应用 RPATH 加载；系统只需稳定 ABI 的 Qt 6、OpenSSL 3、X11 和基础库。这样避免 Ubuntu 22.04 的 FFmpeg 4 库在 Ubuntu 24.04、26.04 已被移除的问题。打包脚本通过 `dpkg-shlibdeps` 推导真实依赖，并显式补充 Qt 平台插件。`.deb` 支持 `sudo apt install ./purr-pause_1.5.0_amd64.deb` 自动安装依赖。

AppImage 则携带 Qt 6、TLS/图片/XCB 插件、OpenSSL 3、视频库、其他非 glibc 依赖、Noto 中文字体和备用 CA 证书。`bundle-appimage-libs.py` 递归检查所有动态库都在 AppDir 中解析，并拒绝要求高于 glibc 2.35 的符号。证书优先使用宿主机当前信任库，备用 CA 仅用于缺少系统证书的环境。它依赖宿主的 Linux 内核、glibc 和 X11/XWayland 桌面；显卡驱动保持使用宿主版本。AppImage type 2 runtime 固定到官方 `20251108` 标签，每种架构均校验 SHA256，具体 URL 和哈希见 `packaging/appimage-runtime.tsv`。

```sh
chmod +x purr-pause-1.5.0-x86_64.AppImage
./purr-pause-1.5.0-x86_64.AppImage
# 无 FUSE 环境同样可以运行：
./purr-pause-1.5.0-x86_64.AppImage --appimage-extract-and-run
```

macOS 使用 Qt 6.5.3 的 `clang_64` 通用二进制包，应用分别构建 Intel 和 Apple Silicon；FFmpeg 6.1.4、libvpx 1.15.2、OpenSSL 3.5.7 从固定源码和 SHA256 校验构建，统一设置 `MACOSX_DEPLOYMENT_TARGET=12.0`。不使用 Homebrew 的 FFmpeg/Qt/OpenSSL 运行库，因为它们的最低系统版本可能高于 macOS 12。Homebrew 仅安装 Ninja、pkg-config、NASM 等构建工具。FFmpeg 启用 libvpx，保留原 WebM 透明通道。

`scripts/bundle-macos-libs.py` 在 macdeployqt 之后递归补全非 Qt 动态库，将通用二进制裁剪到当前包的 CPU 架构，检查每个 Mach-O 文件的最低 macOS 版本，并拒绝同名不同内容的动态库、绝对路径依赖、外部 RPATH 和逃逸应用目录的符号链接。安装检查使用 `--verify-only`，不会修改已签名的文件，也不会回退到构建机搜索路径。该检查证明二进制声明的最低版本，不替代 macOS 12 实机运行验收。Qt、FFmpeg、libvpx 和 OpenSSL 采用动态链接，许可证与对应源码链接放在应用的 `Contents/Resources/licenses`。

## 本地 Linux 构建

```sh
sudo apt update
sudo apt install -y cmake ninja-build pkg-config g++ qt6-base-dev qt6-base-dev-tools \
  libqt6svg6-dev qt6-qpa-plugins libssl-dev libx11-dev libxss-dev libgl1-mesa-dev \
  dpkg-dev xvfb dbus-x11 desktop-file-utils patchelf nasm curl python3 \
  squashfs-tools fonts-noto-cjk fontconfig ca-certificates
export PURR_CODEC_PREFIX="$PWD/.deps/linux-$(dpkg --print-architecture)"
scripts/build-linux-codecs.sh "$PURR_CODEC_PREFIX"
export PKG_CONFIG_PATH="$PURR_CODEC_PREFIX/lib/pkgconfig"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DPURR_CODEC_PREFIX="$PURR_CODEC_PREFIX"
cmake --build build --parallel
dbus-run-session -- xvfb-run -a ctest --test-dir build --output-on-failure
scripts/package-linux.sh build dist
scripts/package-appimage.sh build dist
dbus-run-session -- xvfb-run -a dist/*.AppImage --appimage-extract-and-run \
  --smoke-test --data-dir /tmp/purr-pause-appimage-smoke
```

## 本地 macOS 构建

安装 Xcode Command Line Tools、CMake、Ninja、pkg-config、NASM 和 Qt 6.5.3。Qt 官方安装器或 `aqtinstall` 均可；`clang_64` 包包含两种 CPU 架构。以下命令中的 Qt 路径需替换为本机安装路径：

```sh
export QT_ROOT_DIR="$HOME/Qt/6.5.3/macos"
export PATH="$QT_ROOT_DIR/bin:$PATH"
export PURR_ARCH="$(uname -m)"
export PURR_DEPS_PREFIX="$PWD/.deps/macos-$PURR_ARCH"
export MACOSX_DEPLOYMENT_TARGET=12.0
scripts/build-macos-deps.sh "$PURR_DEPS_PREFIX"
export PKG_CONFIG_PATH="$PURR_DEPS_PREFIX/lib/pkgconfig"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 -DCMAKE_OSX_ARCHITECTURES="$PURR_ARCH" \
  -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR;$PURR_DEPS_PREFIX" -DOPENSSL_ROOT_DIR="$PURR_DEPS_PREFIX"
cmake --build build --parallel
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
scripts/package-macos.sh build/purr-pause.app dist
```

`package-macos.sh` 完成 DMG 后自动调用 `verify-macos-package.sh`。验收步骤包括磁盘映像完整性检查、只读挂载、检查 Applications 快捷方式、复制应用到含空格的独立安装目录、卸载 DMG、校验代码签名和可迁移的动态库，再以 Cocoa 后端运行 `--verify-install`。探针必须返回正确架构、可用 TLS、包内资源路径，以及两段原始 WebM 的实际解码结果、透明与不透明像素；失败会阻止上传正式安装包和发布。

```sh
# 独立复验一个已有的 DMG，不修改其签名：
PURR_ARCH=arm64 scripts/verify-macos-package.sh \
  dist/purr-pause-1.5.0-macos-arm64.dmg dist/verification/macos-arm64
```

DMG 中拖拽应用到 `/Applications` 后再开启登录启动。登录启动记录当前安装路径，移动应用之后应重新切换开关。Linux 使用 XDG autostart，AppImage 记录永久的 `.AppImage` 文件路径。macOS 与原 Electron 33 实现使用相同系统接口：macOS 13 及以上使用 SMAppService，macOS 12 使用 LSSharedFileList 登录项目。成功修改设置后会清理早期原生开发版本的旧 LaunchAgent 配置。登录启动在下一次桌面登录生效。

## macOS 签名与公证

未配置证书时，脚本生成 ad hoc 签名 DMG。它可以用于开发和测试，但没有 Apple Developer ID 信任链及公证，首次运行可能需要在“隐私与安全性”中允许打开。只有配置了真实证书并通过公证的产物，才应向普通用户描述为“已签名且已公证”。

在仓库 Actions Secrets 设置：

| Secret | 用途 |
| --- | --- |
| `MACOS_CERTIFICATE_P12_BASE64` | Developer ID Application 证书及私钥导出的 `.p12` 的 base64 内容 |
| `MACOS_CERTIFICATE_PASSWORD` | `.p12` 导出密码 |
| `MACOS_SIGN_IDENTITY` | 完整身份名称，例如 `Developer ID Application: Example (TEAMID)` |
| `APPLE_ID` | Apple Developer 账号 |
| `APPLE_APP_PASSWORD` | Apple 应用专用密码 |
| `APPLE_TEAM_ID` | Developer Team ID |

前三项启用 Developer ID 签名；全部六项启用提交公证、等待 `Accepted`、staple、stapler 验证和 Gatekeeper 的磁盘映像检查。任何一组只配置部分值都会在构建前明确失败；公证凭据不能单独配置而缺少签名凭据。证书和公证凭据仅在标签任务导入临时 keychain，任务结束清理。PR 不读取证书。签名或公证失败会阻止 release。不要把签名密钥、密码或登录凭据提交到仓库。

本地可设置 `MACOS_SIGN_IDENTITY`，并用 `xcrun notarytool store-credentials` 保存凭据后设置 `MACOS_NOTARY_PROFILE` 来运行相同打包脚本。

## 发布验收

自动安装探针 `--verify-install --verification-report <JSON路径>` 只读取可执行文件相邻的安装资源，不允许使用源码目录回退。它验证 TLS、图标、文档、原始行走/休息视频的透明通道，并实际绘制 Qt 窗口；缺少或损坏资源会返回非零退出码。探针不初始化用户配置、试用记录或登录启动项。Linux 的 `.deb`、AppImage、干净 Ubuntu 22.04 容器和较新 Ubuntu 兼容性任务均运行此探针。

Linux amd64 任务还安装固定的 Electron 33.2.1，加载 `tests/reference/renderer/` 的原样参考页面，使用同一份 `tests/visual/fixture.json`、时区和字体环境分别抓取原版和原生界面。参考 Electron 只用于测试，不打入原生安装包。报告严格检查数据、尺寸和字段绑定，并保留未遮罩的像素、字体和坐标差异；它不会把存在差异的截图标记为“100% 像素一致”。截图、对比图、JSON 与 Markdown 报告位于 `build/visual`，随验证证据上传。

`native-evidence-macos-*` 包含配置/编译日志、CTest 日志、DMG 挂载与签名记录、安装后的依赖检查、Cocoa 安装探针 JSON，以及启用公证时的 Apple 结果。即使任务失败也会上传已经生成的证据。证据附件不混入正式 Release 安装文件。

CI 自动验证单元/集成测试、两个 Linux ABI、安装包依赖、AppImage 的干净 Ubuntu 22.04 环境启动、macOS 架构及最低部署版本。Linux 构建还启用 `PURR_DESKTOP_TESTS=ON`，在 Xvfb、Openbox 和独立 D-Bus 会话内验证真实输入的空闲计时、覆盖窗口点击穿透与交互、全工作区属性、锁屏事件和 GIO 自动启动路径转义。首次公开发布前还应在 Ubuntu 22.04、较新 Ubuntu、macOS 12 Intel/Apple Silicon 和较新 macOS 上验收：透明动画、托盘、多显示器、全屏应用、锁屏、休眠恢复、自动启动、激活配置兼容和通知拉取。Linux 全局覆盖窗口通过 X11/XWayland 定位；原生 Wayland 协议不提供任意全局窗口定位，GNOME Wayland 会优先使用 Mutter 获取完整空闲时间。托盘需要桌面环境的系统托盘支持。

Linux 环境可以直接运行 `python3 -m unittest discover -s tests -p test_macos_packaging.py -v`，验证 Mach-O 解析、递归依赖、冲突、RPATH 和部署版本检查的逻辑。它模拟系统工具输出，不冒充真实 macOS 执行结果。DMG 挂载、codesign、公证和 Cocoa 测试已经自动化，但仍需要实际 macOS runner 执行；缺少该环境属于待执行的自动任务，不是必须手工完成的步骤。以真实 Actions 记录及最低版本系统验收记录为发布依据。
