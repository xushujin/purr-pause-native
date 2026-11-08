# 原版差分测试参考

这里保留 PurrPause 1.5.0 的最小测试参考，原 Electron 工程目录已按用户授权移除。这些文件仅用于验证原生实现的兼容性，不参与原生程序链接或安装，也不进入原生安装包，不能作为完整 Electron 应用启动。

- `behavior/`：从原 `main.js` 按字节提取的配置保存处理器和计时监控函数，由 `tests/test_core.cpp` 在模拟环境中执行。
- `lib/license.js`、`tools/keygen.js`：未经修改的原版授权模块和序列号生成器，用于验证机器码、试用期、激活、续期及持久化格式互通。
- `renderer/`：未经修改的五个表单页面、猫咪页面和火箭页面，供 `scripts/capture-visual-reference.cjs` 与 `docs/audit/animation/capture-reference.cjs` 使用 Electron 33.2.1 采集参考。
- `LICENSE`：原版 MIT 许可和版权声明。

`manifest.json` 记录原始文件路径、原文件 SHA256、参考文件 SHA256，以及行为片段在原文件中的字节偏移、行号和提取边界。迁移时已逐字节核对，后续修改原生实现时应保持这些参考不变。缺少必需参考文件会令差分测试失败；执行行为差分测试还需 Node.js，视觉采集需指定版本的 Electron。

截图、动画参考和验收范围见 `docs/audit/`。保留参考测试与删除旧工程不代表已经通过全部平台验收；实际状态见 `docs/AUTOMATION.md`、`docs/PARITY.md` 和 `docs/ACCEPTANCE.md`。
