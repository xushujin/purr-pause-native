# 原版与原生界面对照

本目录的 `comparison-*.png` 左侧是原版 HTML，右侧是原生 Qt Widgets。各页使用相同配置、来源、待办内容、时区、像素比例与视口；截图不包含操作系统窗口边框。

参考截图使用项目对应的 **Electron 33.2.1 / Chromium 130.0.6723.137** 实际运行原始页面。完整旧工程 `purr-pause/` 已按本次明确批准移除，页面现原样保存在 `tests/reference/renderer/`，可复现工具从该位置加载。独立测试 preload 只提供 `electronAPI` 配置和消息回调，不修改原始 HTML、CSS 或 JavaScript。原生截图由当前 `src/dialogs.cpp` 的真实可交互控件生成，没有嵌入网页或贴图。

环境：Ubuntu 26.04 x86_64；Electron 运行于 Xvfb；原生 Qt 6.10.2 使用 offscreen 平台；`TZ=Asia/Shanghai`；设备像素比例 1。Latin 基础字体为 DejaVu Sans；安装有 Droid Sans Fallback、Noto Sans CJK。两种渲染器的字体回退、抗锯齿与控件度量仍存在差别。

| 页面 | 对照视口 | 文件 |
| --- | --- | --- |
| 常规设置 | 440 × 520 | `comparison-settings.png` |
| 消息设置 | 540 × 660 | `comparison-message-settings.png` |
| 消息列表 | 560 × 720 | `comparison-message-list.png` |
| 激活 | 540 × 540 | `comparison-activation.png` |
| 计时规则 | 950 × 570 | `comparison-rules.png` |

上述尺寸由截图工具固定，目的是比较同一视口。正常启动的设置窗口宽度为 400，计时规则窗口宽度按工作区在 720–900 间选择；截图不代替默认窗口尺寸验收。

`original-*.png` 与 `native-*.png` 保留未拼接图片。`pixel-differences.json` 记录逐像素 RGB 绝对误差，没有做位置对齐、模糊或遮罩。它记录实际差别，**不是验收通过率，也不证明逐像素一致**。背景通道有 0–1 的舍入差异；文字、阴影、部分控件和布局存在较明显差别。

消息列表有一项刻意保留的修正：原版 `.empty { display: grid }` 覆盖浏览器 `[hidden]` 默认规则，使存在消息时仍显示“暂无待办消息”。原生只在列表为空时显示空状态，且以回归测试验证，因此此处不会与原版错误画面相同。

可复现工具已纳入仓库，参见 [tests/visual/README.md](../../../tests/visual/README.md)。`tests/visual/fixture.json` 为两端共享数据；`test_visual` 捕获真实 Qt 控件，`scripts/capture-visual-reference.cjs` 使用指定版本 Electron 捕获原 HTML，`scripts/compare-visual.py` 自动检查数据、尺寸和字段并生成差异报告。

本目录的 `native-manifest.json`、`reference-manifest.json` 记录共享 fixture 的 SHA-256、实际 Qt/Electron/Chromium 版本和字段几何/字体；`visual-report.json` 与 `visual-report.md` 是已实际运行可复现工具产生的结果。自动校验成功不代表像素一致，报告中的 `pixelIdentity` 为 `false`。`tests/reference/` 的最小参考文件只供测试，临时参考 Electron 和截图工具依赖也不会进入原生产品构建或安装包；目录清理不消除已记录的像素差异。
