# 胖猫暂停一下 · 原生版

PurrPause 的 C++17 / Qt Widgets 原生桌面实现，面向 **Ubuntu 22.04 及以上**和 **macOS 12.0 及以上**，提供 x86_64、ARM64 构建。界面、网络请求、系统托盘和视频解码均由本地代码执行，无 Electron、Chromium、WebView 或 Node.js 运行时。

本项目按 PurrPause 1.5.0（Electron 33.2.1）的源码迁移业务逻辑，并复用原版猫咪视频、图标、火箭图形和接口规范。经本次明确批准，完整旧工程 `purr-pause/` 已移除；仅在 `tests/reference/{behavior,lib,tools,renderer}` 保留原样的最小差分参考文件，供开发和测试使用，不参与原生产品构建，也不进入安装包。功能映射、已验证范围及差异见 [复刻对照表](docs/PARITY.md)；删除旧工程不代表已完成“100% 复刻”验收。

## 功能

- 系统托盘常驻、单实例、剩余时间与待办角标、开机启动。
- 按系统闲置、锁屏和睡眠状态统计使用时间；暂停 30 / 60 / 120 分钟或手动恢复。
- 四种猫咪出现方式：中央走出、右下角走出、中央淡入、右侧走出；多显示器休息覆盖层、倒计时、结束休息、最多两次延后 5 分钟。
- 原版 VP9 / Alpha WebM 视频的本地解码；支持自定义目录、走路 / 休息视频和小火箭素材。
- 7 天试用、机器码、原版 Ed25519 序列号验证与授权文件兼容。
- 多接口源待办轮询、来源筛选、连接测试、首次同步静默、失败退避、浏览器打开后本地清理、休息期间合并通知。
- GitHub Actions 在拉取请求中构建与测试，发布标签触发 `.deb` / AppImage / `.dmg` 打包及 GitHub Release。

## 安装和运行

从仓库 Releases 下载对应架构的软件包。版本号以实际发布文件为准。

| 系统 | x86_64 / Intel | ARM64 / Apple Silicon |
| --- | --- | --- |
| Ubuntu 22.04+ | `purr-pause_1.5.0_amd64.deb` | `purr-pause_1.5.0_arm64.deb` |
| Ubuntu 22.04+ 便携版 | `purr-pause-1.5.0-x86_64.AppImage` | `purr-pause-1.5.0-aarch64.AppImage` |
| macOS 12.0+ | `purr-pause-1.5.0-macos-x86_64.dmg` | `purr-pause-1.5.0-macos-arm64.dmg` |

Ubuntu 使用 APT 安装本地包，以便同时安装运行依赖：

```bash
sudo apt install ./purr-pause_1.5.0_amd64.deb
purr-pause
```

AppImage 携带 Qt、视频解码库与中文字体，无需安装这些依赖：

```bash
chmod +x purr-pause-1.5.0-x86_64.AppImage
./purr-pause-1.5.0-x86_64.AppImage
# 无 FUSE 环境：
./purr-pause-1.5.0-x86_64.AppImage --appimage-extract-and-run
```

macOS 打开 DMG，将应用复制到 Applications，再启动应用。签名和公证取决于发布仓库是否配置 Apple 开发者凭据，见 [发布说明](docs/RELEASING.md)。

启动后从托盘菜单打开“设置”“消息提醒设置”“待办消息”“计时规则”或“立即测试”。桌面未提供系统托盘时，应用显示菜单入口窗口。

Ubuntu Wayland 会话下使用 XWayland 提供覆盖层的屏幕定位，GNOME Mutter 提供全局闲置时间；应用在存在 `DISPLAY` 时默认选择 Qt `xcb` 后端。纯 Wayland、未安装 XWayland 或其他合成器环境的置顶、跨应用定位和闲置检测需要单独验证。Ubuntu 默认 GNOME 环境建议保留系统 AppIndicator 托盘扩展。

## 从源码构建

### Ubuntu

发布包必须在 Ubuntu 22.04 构建，才能避免较新系统的 glibc、Qt、FFmpeg ABI 抬高最低运行要求。开发时可以使用更新的 Ubuntu；这些机器生成的包只按其实际依赖使用。

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build pkg-config dpkg-dev \
  qt6-base-dev qt6-base-dev-tools libqt6svg6-dev \
  libavformat-dev libavcodec-dev libavutil-dev libswscale-dev \
  libssl-dev libx11-dev libxss-dev libgl1-mesa-dev curl nasm patchelf

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/purr-pause --settings
```

发布兼容 Ubuntu 22.04 及后续版本的包时，在 Ubuntu 22.04 上执行下面步骤。视频库随应用放入私有目录，避免不同 Ubuntu 的 FFmpeg ABI 包名变化导致无法安装：

```bash
export PURR_CODEC_PREFIX="$PWD/.deps/linux-$(dpkg --print-architecture)"
bash scripts/build-linux-codecs.sh "$PURR_CODEC_PREFIX"
export PKG_CONFIG_PATH="$PURR_CODEC_PREFIX/lib/pkgconfig"
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DPURR_CODEC_PREFIX="$PURR_CODEC_PREFIX"
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure
bash scripts/package-linux.sh build-release dist
# 额外安装 squashfs-tools、fonts-noto-cjk、fontconfig、ca-certificates 后：
bash scripts/package-appimage.sh build-release dist
```

ARM64 使用原生 ARM64 主机或对应的 GitHub Actions runner 执行相同命令。Qt、OpenSSL 和桌面系统库仍使用发行版软件包；依赖由打包工具按实际链接结果生成。

### macOS

使用 Xcode Command Line Tools、CMake、Ninja、pkg-config、NASM 和 **Qt 6.5.3 的 macOS `clang_64` SDK**。CI 固定此 Qt 版本，并将应用和自行编译的 FFmpeg、libvpx、OpenSSL 的 deployment target 设置为 `12.0`；不要直接用面向较新 macOS 的 Homebrew 动态库替换发布依赖。

以下示例中，`/path/to/Qt/6.5.3/macos` 替换为本机 Qt SDK 路径：

```bash
export MACOSX_DEPLOYMENT_TARGET=12.0
export PURR_ARCH="$(uname -m)"
export PURR_DEPS_PREFIX="$PWD/.deps/macos-$PURR_ARCH"
bash scripts/build-macos-deps.sh "$PURR_DEPS_PREFIX"
export PKG_CONFIG_PATH="$PURR_DEPS_PREFIX/lib/pkgconfig"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
  -DCMAKE_OSX_ARCHITECTURES="$PURR_ARCH" \
  -DCMAKE_PREFIX_PATH="/path/to/Qt/6.5.3/macos;$PURR_DEPS_PREFIX" \
  -DOPENSSL_ROOT_DIR="$PURR_DEPS_PREFIX"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
open build/purr-pause.app
```

依赖打包、代码签名、公证与 DMG 命令的完整参数见 [发布说明](docs/RELEASING.md)。

## 配置、素材和兼容性

原生版沿用原项目的数据目录和 JSON 字段：

| 数据 | Linux | macOS |
| --- | --- | --- |
| 应用数据目录 | `${XDG_CONFIG_HOME:-~/.config}/purr-pause/` | `~/Library/Application Support/purr-pause/` |
| 用户配置 | 数据目录下 `config.json` | 同左 |
| 用户素材 | 数据目录下 `webm/` | 同左 |
| 运行日志 | 数据目录下 `logs/` | 同左 |

旧单源消息配置会转换为 `messageSources`。授权读取保留原版签名、完整性校验和机器码算法；操作系统或架构改变会影响机器码。消息正文、已知 ID 和本地清理记录不写入磁盘。接口认证请求头属于用户配置，会保存在配置文件中，但不写入运行日志。

素材仍使用 `cat-walk.webm`、`cat-rest.webm` 和可选 `notify-rocket.webm`。视频要求 VP9 编码及 Alpha 通道；默认猫咪素材直接来自原项目。小火箭素材查找顺序为：消息素材目录 → 通用素材目录 → 用户 `webm/` → 内置素材；未找到视频时使用内置矢量火箭。

使用独立目录可以验证原生版而不修改现有配置：

```bash
./build/purr-pause --data-dir /tmp/purr-pause-native-demo --settings
./build/purr-pause --data-dir /tmp/purr-pause-native-demo --test-break
./build/purr-pause --data-dir /tmp/purr-pause-native-demo --rocket-demo
```

同一数据目录只启动一个原生进程；测试不同启动参数时先退出已运行的实例。原版 Electron 和原生版使用不同的进程锁，迁移配置时应先关闭原版的开机启动并退出原版，避免两套程序同时写入同一配置。

## 消息接口

每个消息源填写完整的 HTTP(S) 列表 URL 及 JSON 请求头。客户端保留既有查询参数，覆盖 `limit` 和 `offset=0`，每源缓存 1–30 条，默认每 60 秒轮询。

成功响应的 `code` 必须为数字 `0`，待办需有稳定 `id`、非空 `title` 和 HTTP(S) `targetUrl`。角标只统计本地保留条数。首个成功轮询建立基线；后续新增才播放动画。点击待办并成功调用默认浏览器后，仅清理本地条目，无写回接口。

完整协议见 [消息提醒接口规范](docs/message-api-spec.md)。原生网络层增加 1 MiB 响应上限、请求头换行校验及同源重定向限制；跨域或跨协议重定向应改为直接配置最终接口 URL。

## GitHub Flow 与发布

1. 从 `main` 创建功能分支，提交原生代码、相应测试及文档。
2. 创建 Pull Request；`Native build and release` 工作流在 Linux、macOS 两种架构上构建、测试及生成安装包。
3. 检查测试结果和构建产物，合并到 `main`。
4. 更新 `CMakeLists.txt` 版本，推送对应的 `v<版本>` 标签；工作流上传安装包到 GitHub Release。

手动 `workflow_dispatch` 可用于构建验证。工作流位置、权限、签名密钥和版本匹配规则见 [docs/RELEASING.md](docs/RELEASING.md)。当前工作区已准备流程；远程 CI 的执行结果以 GitHub Actions 为准。

## 验证与目录

原有十类功能测试包括：计时与原版授权差分、消息接口、媒体、动画、单实例、对话框、完整应用、启动、X11 系统集成和双显示器。Linux 桌面测试通过 `-DPURR_DESKTOP_TESTS=ON` 启用，需额外安装 `openbox libglib2.0-bin libxtst-dev xvfb dbus-x11`。原版 Electron 33 的真实截图及动画采样见 [视觉对照](docs/audit/screenshots/README.md)和[动画证据](docs/audit/animation/README.md)。各平台实际验证状态见 [对照表](docs/PARITY.md)。

自动验收另外包括安装资源/TLS/透明视频的正反例检查、macOS 动态库图测试、可重复的视觉对照和 Cocoa/DMG 安装检查；Linux 启用桌面测试时当前共 12 个 CTest 目标。`purr-pause --verify-install --verification-report report.json` 会直接验证已安装软件，不使用源码目录兜底，也不修改用户配置。完整实现与实际执行状态见 [自动验收说明](docs/AUTOMATION.md)。

```text
src/                    原生 C++ / Objective-C++ 实现
tests/                  QtTest 自动化测试
tests/reference/        原样保留的最小行为、授权及界面差分参考，仅供测试
assets/                 原项目猫咪视频、图标及火箭图形
packaging/              desktop entry、应用清单及安装元数据
scripts/                依赖构建、Linux / macOS 打包脚本
.github/workflows/      GitHub Actions 构建与发布
docs/                   接口规范、复刻对照与发布文档
```

项目代码使用 [MIT 许可证](LICENSE)。Qt、FFmpeg、libvpx、OpenSSL 的第三方许可和对应源码信息由打包脚本一并处理。

猫咪视频沿用原项目素材；感谢原项目及素材贡献者 [louiscmli0719](https://github.com/louiscmli0719)。
