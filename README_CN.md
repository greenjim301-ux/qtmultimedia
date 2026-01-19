# 变更报告

## 变更摘要
此分支主要致力于导出 AVFrame 渲染 API，并为 QtMultimedia 中的 FFmpeg 后端引入 DRM (Direct Rendering Manager) 硬件加速支持。

此修改已在 Qt 6.11.0 上进行测试。

### 主要修改
*   **DRM 渲染支持**: 实现了专门针对 FFmpeg Rockchip MPP 解码的 AVFrame 的 DRM 帧渲染支持，新增文件为 `src/plugins/multimedia/ffmpeg/qffmpeghwaccel_drm.cpp` 和 `src/plugins/multimedia/ffmpeg/qffmpeghwaccel_drm_p.h`。
*   **渲染 API**: 导出了允许用户直接渲染 AVFrame 的 API。
*   **代码风格**: 添加了 `.clang-format` 配置文件以标准化代码格式。
