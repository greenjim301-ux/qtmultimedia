# Change Report

中文请参考 [README_CN.md](README_CN.md)

## Summary of Changes
This fork primarily focuses on exporting the AVFrame render API and introducing DRM (Direct Rendering Manager) hardware acceleration support for the FFmpeg backend in QtMultimedia.

This modification is tested with Qt 6.11.0.

### Key Modifications
*   **DRM Render Support**: Implemented DRM frame render support specifically for [FFmpeg Rockchip](https://github.com/nyanmisaka/ffmpeg-rockchip) MPP decoded AVFrame, with new files `src/plugins/multimedia/ffmpeg/qffmpeghwaccel_drm.cpp` and `src/plugins/multimedia/ffmpeg/qffmpeghwaccel_drm_p.h`.
*   **Render API**: Exported API to allow users to directly render AVFrame.
*   **Code Styling**: Added `.clang-format` configuration file to standardize code formatting.
