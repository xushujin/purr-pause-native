# 原版与原生实现验收记录

对照基准为 PurrPause 1.5.0 / Electron 33.2.1。完整旧工程 `purr-pause/` 已按本次明确批准移除，原样保留的最小差分参考位于 `tests/reference/{behavior,lib,tools,renderer}`，仅用于开发和测试，不参与原生产品构建或安装包。原生版本使用 C++17、Qt Widgets、FFmpeg/libvpx 和平台 API，没有 Electron、Chromium、WebView 或 Node.js 产品运行时。Node 和 Electron 只用于开发期差分测试。

更新日期：2026-11-08。**目前不能宣称全部目标平台已经达到经过验证的 100% 复刻。** 已补齐本地可复现的功能缺口，并保留真实测试和图像证据；macOS 自动构建/安装因执行环境缺失而尚未运行，与物理桌面人工体验分别列出。自动步骤的实现和实际执行状态见 [AUTOMATION.md](AUTOMATION.md)。本次明确批准删除完整旧工程，取代此前“完整验收后才删除”的条件；必要参考已迁移，验收限制仍然有效。

## 功能映射与证据

| 原版能力 | 原生实现 | 当前证据 |
| --- | --- | --- |
| 托盘常驻、剩余时间、暂停/恢复、消息数、设置、规则、日志、立即测试、独立火箭演示、退出 | `src/application.cpp` | 菜单文案/顺序对照源码；完整应用测试；真实托盘服务仍需目标桌面验收 |
| 单实例 | `src/main.cpp` 的 QLockFile | 重复启动退出、退出后锁释放、再次运行测试 |
| 每 10 秒采样；空闲 <180000ms 累加，>600000ms 清零 | `BreakTimer`、平台空闲 API | 直接执行原版 main.js 回调差分；XTest 真实输入后的 XScreenSaver 毫秒值验证 |
| 暂停 30/60/120 分钟或无限暂停 | `BreakTimer`、独立到期 QTimer | 边界/恢复测试；恢复不再等待下一个采样 |
| 锁屏、休眠及超过 10 分钟清零 | 平台事件 + `BreakTimer` | 独立 D-Bus 上的真实 ScreenSaver/login1 消息投递和对象生命周期；真实关机睡眠仍需物理机器 |
| 最多两次延后 5 分钟、关闭后 60 秒冷却 | `BreakTimer`、`BreakOverlay` | 计时、延后上限及自动结束测试 |
| 四种透明猫咪动画 | `src/overlays.cpp`、`src/media.cpp` | 官方 Electron 33.2.1 执行原 HTML：80 个几何样本最大差异 0.028076 CSS 像素；原素材 Alpha 解码验证 |
| 自定义素材比例、走路一次、躺下循环、加载失败、淡入淡出和倒计时 | `BreakOverlay`、`VideoPlayer` | 竖向 WebM fixture、最终帧时长、错误回退时序、切换陈旧事件隔离；保留 BT.709 色彩元数据 |
| 多显示器同步覆盖与关闭 | 每屏独立窗口、共享解码器 | 两个独立 X11 screen（1280×900 / 1024×768）实际创建、绑定、尺寸和关闭验证；物理混合 DPI/热插拔仍需验收 |
| 点击穿透、按钮可交互、所有工作区 | Qt + X11 EWMH/Shape；macOS AppKit | Openbox 下真实鼠标点击、恢复输入、工作区切换验证；修复 WM 外框拦截透明点击 |
| 自定义素材、文件优先级、说明文档 | `Application::asset/writeAssetReadmes` | 原文说明文件保存在 `assets/readme`；素材优先级及路径校验 |
| Linux 自启动 | XDG Desktop Entry | GIO 实际启动含空格、单双引号、美元、反引号、百分号和反斜杠的程序路径；添加/移除验证 |
| macOS 自启动 | macOS 13+ SMAppService、macOS 12 LSSharedFileList | 与 Electron 33 的真实平台源码使用相同接口；bundle ID 保持 `com.purr-pause.app`；仍缺 Mac 实测 |
| 原版机器码、7 天试用、激活、续期、永久授权、防复用 | `LicenseManager`、激活窗口 | 原版 keygen 生成本机序列号，逐项比较成功激活、重复拒绝、30/90/365 天及永久授权、完整持久化 JSON/HMAC；激活窗口端到端成功反馈及延迟关闭 |
| 试用日期和配置迁移 | `ConfigStore`、`LicenseManager` | 原 JS 差分、上海/纽约夏令时与到期前后 1ms、旧单源迁移、未知字段保留、原子保存 |
| 多来源待办、首次静默、独立轮询、退避、纯文本、浏览器跳转后本地清理 | `MessageService`、消息列表 | 本地真实 HTTP 服务、非法/边界响应、字段转换、时区、排序、禁用后旧请求隔离、5 秒超时 |
| 休息与火箭同时发生时通知合并 | `Application` 的单一播放队列 | “已有火箭→新增A→开始休息→新增B→结束休息”一次合并 A+B；独立演示不影响业务队列 |
| 独立消息设置及连接测试 | `MessageSettingsDialog` | 一次一个连接测试、表单校验/展开/保留、15 秒 UI watchdog、当前缓存上限、保存后不关闭及浮层反馈 |
| 原版五页中文界面 | `src/dialogs.cpp` | 相同配置/视口下的实际 Electron 33.2.1 与原生控件截图，见视觉证据；存在非零像素差异 |
| 日志和接口规范导出 | `Application`、原始接口文档 | 保留开关、128KiB/5文件轮转、UTC+8、不记录认证头或消息正文；内部诊断内容不逐行复制 Electron |
| deb / AppImage / DMG 与 GitHub Flow | `scripts/`、`.github/workflows/native.yml` | amd64 deb 已构建；独立 AppImage 已在无 Qt/FFmpeg 的干净 Ubuntu 22.04 环境运行并解码视频；macOS 实际流水线仍待执行 |

## 视觉和动画对照

- [五页截图、对照条件及像素误差](audit/screenshots/README.md)。参考引擎为 **Electron 33.2.1 / Chromium 130.0.6723.137**，使用原始 HTML/CSS/JS，只替代配置回调；原生侧为真实 Qt 控件。页面布局、行高、文案、开关、折叠区域、浮层、滚动条和背景已据截图调整。
- [动画原始数据和可重复捕获脚本](audit/animation/README.md)。猫咪 4 模式 × 4 宽高比 × 5 时刻共 80 个样本；火箭 8 个时刻的 CSS 变换/透明度。几何误差已量化，但不能冒充所有像素完全相同。
- 字体回退/字距/栅格化、部分阴影和控件度量仍有差异。`pixel-differences.json` 没有做模糊、遮罩或对齐，记录的非零结果是实际差异，不是“完成百分比”。
- 保留一项原版错误修正：原 HTML 的 `.empty { display:grid }` 覆盖 `[hidden]`，在有消息时仍显示空状态。原生只在无消息时显示空状态，测试覆盖此行为。

## 本地验证

Ubuntu 26.04 x86_64、GCC 15.2、Qt 6.10.2：本轮完整构建 **12/12 CTest 目标通过**；最后的表单几何修复另执行受影响的 dialogs/application/installation 回归通过。下表数量包含 QtTest 初始化和清理。

| 目标 | 结果 | 重点 |
| --- | --- | --- |
| core | 18 通过、0 失败、0 跳过 | 计时/配置/原版授权差分、本机实际签名激活续期 |
| messages | 15 通过、0 失败、0 跳过 | 多源 HTTP 状态机和安全边界 |
| media | 9 通过、0 失败、0 跳过 | Alpha、循环、帧时长、原始高分辨率、错误和线程回调隔离 |
| ui | 13 通过、0 失败、0 跳过 | 原版动画样本、四模式、比例与失败回退 |
| dialogs | 10 通过、0 失败、0 跳过 | 五页表单/状态/筛选/保存/激活反馈 |
| application | 5 通过、0 失败、0 跳过 | 保存持久化、通知合并、激活完整窗口流程 |
| instance | 3 通过、0 失败、0 跳过 | 单实例锁及重启 |
| platform | 7 通过、0 失败、0 跳过 | X11/Openbox/D-Bus/GIO 实际交互 |
| screens | 3 通过、0 失败、0 跳过 | 两个独立 X11 屏幕与同步关闭 |
| app_smoke | 成功退出 | TLS 后端和原生程序启动 |
| installation | 3 通过、0 失败、0 跳过 | 有效、缺失、损坏安装资源的实际进程验证，禁止源码目录兜底 |
| macos_packaging | 16 个 Python 用例通过 | Mach-O 依赖图、架构/最低版本、冲突、相对路径、已部署副本身份与只读安装验证 |

视觉比较工具额外有 6 个 Python 回归用例，覆盖尺寸/版本/fixture/字段错误、错误隐藏或禁用的控件、像素差异不冒充一致。五页使用共享 fixture 的实际 Electron/Qt 捕获和比较已执行，报告保留非零像素差异。

Mac 平台模块已使用真实 SDK 13.3 和官方 Qt 6.5.3 对 Intel/ARM64 编译为 Mach-O，并完成该模块的动态库链接；main/application/dialogs/overlays/messages/installation 六个 C++ 编译单元也通过两种 Mac 目标的对象编译。`test_macos_platform.mm` 的双目标类型检查通过。它们不能替代完整应用链接或 Mac 运行，证据见 [macos-compilation.json](audit/macos-compilation.json)。

Ubuntu 22.04 x86_64 用户空间使用 GCC 11.4、Qt 6.2.4 和私有 FFmpeg 6.1.4/libvpx 1.15.2 构建；完整十组测试通过，原版参考工具使用官方 Node 20，0 跳过。后续托盘文案、精确定时器、X11 多 screen 根窗口和自定义高分辨率素材修改均执行相关回归。

ARM64 使用 Ubuntu 22.04 用户空间、GCC 11 交叉编译器与对应 Qt 6.2.4，测试执行的是实际 ARM64 ELF，通过 QEMU/PRoot 运行。消息、媒体、UI、单实例、对话框、完整应用、启动、X11平台和双屏九组通过；core 在补齐原版参考源码并给参考 Node 设置 `NODE_OPTIONS=--jitless` 后通过。该设置仅解决模拟器下 V8 JIT 崩溃，不是产品要求。构建工具使用原生 amd64 CMake，避免模拟器内 CMake 自动生成进程崩溃；生成的应用和测试仍是 ARM64。该验证不能替代 ARM 物理桌面。

ARM64 的 FFmpeg 构建还实际暴露并修复了上游无 shebang 脚本在 QEMU 中导致空 SONAME 的问题；构建脚本现在为其明确指定 shell，最终库版本和 `$ORIGIN` 均检查通过。macOS 当前没有可执行的构建主机，尚无 DMG 运行证据；自动 Cocoa/DMG 检查已实现，并通过真实 SDK 双目标编译检查，等待 runner 执行。

AppImage 的 ELF 闭包检查拒绝高于 glibc 2.35 的符号，除 glibc/系统加载器外的依赖均在 AppDir 中解析；Qt TLS 的 OpenSSL 动态加载也包含在内。官方 type-2 runtime 两架构均固定版本和 SHA256。实际最小 rootfs 无系统 Qt/FFmpeg，应用和 VP9 视频均成功运行。AppRun 使用相对 RPATH/qt.conf，避免把私有 Qt/动态库环境传给系统浏览器。

最终本地产物已生成并核验 `dist/SHA256SUMS`：

| 文件 | 大小 | 运行证据 |
| --- | --- | --- |
| `purr-pause_1.5.0_amd64.deb` | 11,668,352 字节 | Ubuntu 22.04 安装/启动及原版 VP9 解码；Ubuntu 26.04 兼容检查 |
| `purr-pause_1.5.0_arm64.deb` | 11,589,966 字节 | Ubuntu 22.04 ARM64 用户空间在 QEMU 下实际安装/启动及 VP9 解码 |
| `purr-pause-1.5.0-x86_64.AppImage` | 62,609,912 字节 | Ubuntu 26.04 运行；干净 Ubuntu 22.04 无 Qt/FFmpeg 环境下，完整 AppImage `--appimage-extract-and-run` 启动及 VP9 播放通过 |
| `purr-pause-1.5.0-aarch64.AppImage` | 61,594,120 字节 | 干净 ARM64 Ubuntu 22.04 无 Qt/FFmpeg 环境下，完整 AppImage 在 QEMU 中 `--appimage-extract-and-run` 启动及 VP9 播放通过 |

[机器可读验证记录](audit/verification.json)保存源码/夹具 SHA256、参考 Electron 归档 SHA256、四个产物大小和 SHA256。Linux 两种架构构建快照与当前产品源码逐文件核对一致。新增安装探针已对四个最终产物实际执行；[五份报告](audit/install/README.md)记录架构、安装资源位置、TLS 和透明视频结果。QEMU 特有的构建工具、参考 Node 和本地打包暂存处理已与产品运行要求区分；CI 仍在原生架构 runner 上构建。

## 仍需验收的项目

| 项目 | 尚缺的证据 |
| --- | --- |
| macOS Intel / Apple Silicon | 实际编译、DMG 安装、macOS 12 及较新版本运行、AppKit 跨 Space/全屏/点击行为、系统登录项；不能用 deployment target 代替这些结果 |
| 物理桌面 | GNOME X11/Wayland、混合 DPI 多显示器、真实热插拔、真实锁屏/睡眠/恢复/重登、自启动；虚拟 X11 测试仅覆盖其中一部分 |
| 逐像素一致 | 现有截图明确存在字体及原生绘制差异；几何和业务行为测试不等于逐像素相同 |
| macOS 签名、公证 | 需要仓库已配置真实 Developer ID 和 Apple 凭据；缺省产物是 ad hoc 签名，不能描述成已公证 |
| GitHub 远程工作流 | 文件静态检查通过，但本会话没有远程成功运行和 Release 记录；本地测试不等于远程发布成功 |

用户本次已明确批准删除完整旧项目并保留最小差分参考，因此旧工程清理不再以上述全部验收通过为前提。`tests/reference/` 保留兼容性回归所需的原样参考文件；目录清理不改变表中仍缺的 macOS、物理桌面、像素一致性及远程发布证据。

## 明确保留的实现差异

- 配置保存先完整校验再原子提交；原版某些失败路径会修改部分内存字段。
- 网络响应上限 1 MiB，拒绝 CR/LF 请求头注入，认证头不跟随跨源重定向；无业务消息落盘。
- 禁用/修改接口时取消并隔离过期请求，避免旧结果复活。
- 永久授权内部 `daysLeft=-1` 表示原 JavaScript 的 Infinity，界面仍显示“永久”。
- 对不符合接口规范的任意 V8 日期文本或畸形配置，不承诺复制所有解析副作用。
- 原生和 Electron 使用不同实例锁，迁移时先退出旧程序。相同安装位置/bundle ID 的 macOS 登录项使用原系统接口；应用搬迁仍应重新切换登录启动设置。
