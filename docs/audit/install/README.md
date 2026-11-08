# Linux 安装探针记录

本目录 JSON 由最终安装包的 `--verify-install` 实际执行生成。两种 `.deb` 均安装到 Ubuntu 22.04.5 用户空间；两种完整 AppImage 均在不含 Qt/FFmpeg 的 Ubuntu 22.04.5 最小用户空间使用 `--appimage-extract-and-run` 执行，连接 Xvfb X11 服务。AMD64 AppImage 另在 Ubuntu 26.04 主机执行。

ARM64 程序通过 QEMU user-mode/PRoot 执行，属于真实 ARM64 二进制的仿真验证，不替代 ARM64 物理桌面验收。AMD64 直接执行。全部记录必须 `ok: true`，包含 OpenSSL TLS 后端、两个 1920×1080 原始视频及透明/不透明像素验证。资源路径来自安装目录或 AppImage 解压目录，未回退源码。临时路径反映测试当时的环境。

本次最终 UI 调整后，两种架构分别重跑 dialogs、installation、app_smoke 三组并全部通过。installation 覆盖有效资源成功、缺失资源失败、损坏视频失败。Node 20 原版对照测试此前完成；ARM64 使用 `NODE_OPTIONS=--jitless` 避免模拟器内 V8 JIT 崩溃，未更改测试断言。

## 已验证安装包 SHA256

```text
c69e19fd4dc3aced4084f11f74e2f87870dfd62df19e4b431b7f96cd5c746bf0  purr-pause-1.5.0-aarch64.AppImage
c96e7a918285ae94a9970d7585611982e18032b9a3bf4ed7edb77be84b14871a  purr-pause-1.5.0-x86_64.AppImage
4445c4836bbbfe1d8f961a5c22286bf6623b69eea1472bf180a22eeff7176533  purr-pause_1.5.0_amd64.deb
d16c5fcb11ae738fef4b4231574c6815c556216f88b1561aa1a7f4c7f3c6dd19  purr-pause_1.5.0_arm64.deb
```
