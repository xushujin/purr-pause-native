# 原版动画对照数据

基准程序为项目原始 Electron **33.2.1**（Chromium 130），直接加载 `tests/reference/renderer/index.html` 与 `rocket-demo.html`。完整旧工程 `purr-pause/` 已按本次明确批准移除；这些页面属于原样保留的最小差分参考，仅供测试，不参与原生产品构建或安装包。截取过程不改写原版动画公式。

猫咪对照设置 1280×800 工作区，模拟视频元数据，并控制原版 `requestAnimationFrame` 时间。覆盖四种动画、四种素材宽高比、五个进度，共 80 组几何与透明度。原生生产代码 `animation::catPose()` 对全部样本的最大差异为 **0.028076 CSS 像素**，来自 Chromium 的布局量化。`tests/test_ui.cpp` 保留了代表样本作为回归测试。

火箭对照直接暂停原版 CSS Web Animations，记录 8 个时刻的位移、缩放、透明度、火焰、火星与文本几何。生产代码 `animation::rocketPose()` 的位移误差小于 0.001 CSS 像素，其余参数误差小于 0.00001。

运行条件：Node.js、Playwright、官方 Electron 33.2.1 和 Linux Xvfb；macOS 可省略 `xvfb-run`。Playwright 仅用于离线验证，产品没有浏览器依赖。

```bash
npm install --prefix /tmp/purr-reference playwright
NODE_PATH=/tmp/purr-reference/node_modules \
PURR_REFERENCE_ELECTRON=/path/to/electron-33.2.1/electron \
PURR_REFERENCE_OUTPUT=/tmp/purr-reference/output \
xvfb-run -a node docs/audit/animation/capture-reference.cjs "$PWD"
```

几何测试不等于所有平台逐像素一致：字体栅格化、原生窗口合成、按钮阴影和 GPU 视频缩放仍由系统及渲染器决定。macOS 12 的实机图像与输入验收仍须执行。
