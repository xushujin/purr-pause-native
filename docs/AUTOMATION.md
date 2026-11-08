# 自动验收的实现与执行状态

本文件区分“自动步骤已经实现”“自动步骤实际通过”“缺少环境而未执行”与“需要实机人工体验”。缺少 CI 登录或签名凭据不属于人工验收。

完整旧工程 `purr-pause/` 已按本次明确批准移除；自动差分测试改用 `tests/reference/{behavior,lib,tools,renderer}` 中原样保留的最小参考文件。这些文件及测试期 Node/Electron 不参与原生产品构建，也不进入安装包。移除旧工程不代表未执行的自动任务或人工验收已经通过。

迁移后在旧目录缺席的情况下重新构建，12 个 CTest 目标全部通过，Qt 测试无跳过；五页视觉采集校验、88 组原版动画样本复现、6 项视觉工具测试、工作流检查和安装探针均通过。原生产品源码、素材和已有发布产物哈希保持一致，清理验证记录见 [audit/legacy-cleanup.json](audit/legacy-cleanup.json)，参考来源和哈希见 [tests/reference/manifest.json](../tests/reference/manifest.json)。

## 当前可自动执行的检查

| 检查 | 实现 | 执行状态 |
| --- | --- | --- |
| 计时、消息、授权、窗口、视频、双屏、X11/D-Bus | `tests/`、CTest | Linux x86_64 与 ARM64 已执行；ARM64 使用 QEMU，限制见 PARITY |
| 安装资源与运行依赖 | `purr-pause --verify-install --verification-report report.json` | 已检查有效、缺失、损坏资源；实际 deb/AppImage 已执行 |
| 原版/原生五页对照 | `test_visual`、`capture-visual-reference.cjs`、`compare-visual.py` | exact Electron 33.2.1 的实际双端截图、尺寸/字段验证和无掩码差异报告已执行 |
| macOS 动态库图 | `test_macos_packaging.py` | 16 个自动用例通过，覆盖转移、架构、版本、冲突、符号链接逃逸、不可迁移路径及已部署动态库的内容身份 |
| macOS 平台代码编译/链接 | 实际 SDK 13.3、官方 Qt 6.5.3、Clang/LLD | Intel、ARM64 的平台模块编译和链接已通过；这不是完整应用构建或运行 |
| Cocoa 窗口与平台行为 | `test_macos_platform`、`cocoa_ui` | 已实现并完成双目标 SDK 类型检查；等待 macOS runner 实际运行 |
| macOS DMG 安装验证 | `verify-macos-package.sh` | 已实现：只读挂载、拷贝到含空格路径、卸载后启动、只读签名/依赖校验、Cocoa 安装探针；等待 macOS runner |
| Developer ID 签名与 Apple 公证 | workflow 凭据预检、`package-macos.sh` | 已实现；等待真实证书和 Apple 凭据执行 |
| GitHub Flow / Release | `.github/workflows/native.yml` | 语法和脚本检查已通过；尚无本会话的远程运行记录 |

安装探针从安装位置查找图标、原始视频、素材说明和接口规范，不允许回退到开发机源码目录。它实际加载 TLS 后端、解码 walk/rest 两段视频、检查透明和非透明像素并绘制原生窗口，失败返回非零退出码。它不读取或修改用户配置、授权或自启动项。

macOS DMG 检查会验证 `hdiutil verify`、Applications 链接、Info.plist、完整资源、签名、每个 Mach-O 的架构及最低版本、真实相对动态库路径；配置公证时还检查 Apple `Accepted`、staple 和 Gatekeeper。验证过程中清除构建机 Qt/DYLD 环境变量，防止借用开发依赖。

视觉自动化对输入数据、版本、时区、尺寸与字段内容严格校验，对字体/几何/像素差异直接报告。`captureValidation=passed` **不表示** `pixelIdentity=true`；没有掩码、模糊或把像素差异换算为“完成比例”。

## CI 如何执行

- PR、主分支推送或手动运行执行构建与自动验收，不创建 Release。
- Linux x86_64 job 使用固定 Electron 33.2.1 生成视觉报告；两种架构均验证 deb 和完整 AppImage，另在无 Qt/FFmpeg 的 Ubuntu 22.04 容器运行安装探针。
- macOS Intel/ARM runner 执行普通 Qt 测试、Cocoa 测试、打包和复制安装后的验证。
- 构建失败时也上传 `native-evidence-*`，包含 CTest、配置/构建/安装日志、JSON 报告及视觉图片。
- 版本标签与 CMake 版本完全一致、所有任务通过后才运行发布任务；签名或公证凭据只提供一部分会在构建前明确失败。

具体命令、产物、Secrets 名称见 [RELEASING.md](RELEASING.md)。测试工具需要 Python 3.10+；视觉比较额外需要 Pillow。它们不是安装后软件的运行依赖。

## 仍需提供的执行条件

当前工作区是 Linux，没有 GitHub 登录凭据、SSH Mac 配置或可调用的 macOS 主机，仓库也尚无本地提交。因此无法在这里实际触发和等待 macOS GitHub runner、执行完整 Mac 应用/DMG、提交 Apple 公证或发布 GitHub Release。工作流实现完成不等于这些步骤已经执行成功。

需要的是已登录的 GitHub CLI/仓库访问环境或已有 macOS CI 主机；签名公证另外需要仓库 Secrets 中的 Developer ID 和 Apple 凭据。不要把密码或私钥放入聊天或提交到源码。

## 真正需要实机体验的内容

真实 GNOME Wayland、物理热插拔与混合 DPI、真实系统睡眠/解锁/重新登录，以及 macOS 多 Space/全屏等体验仍按 [ACCEPTANCE.md](ACCEPTANCE.md)记录。它们与上面的自动执行环境缺口分别管理。
