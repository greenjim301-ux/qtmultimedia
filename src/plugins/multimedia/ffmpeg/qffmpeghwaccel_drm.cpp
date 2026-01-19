// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qffmpeghwaccel_drm_p.h"

#if !QT_CONFIG(vaapi)
#  error "Configuration error"
#endif

#include <qvideoframeformat.h>
#include "qffmpegvideobuffer_p.h"
#include "private/qvideotexturehelper_p.h"

#include <rhi/qrhi.h>

#include <qguiapplication.h>
#include <qpa/qplatformnativeinterface.h>

#include <qopenglfunctions.h>

#if __has_include("drm/drm_fourcc.h")
#  include <drm/drm_fourcc.h>
#elif __has_include("libdrm/drm_fourcc.h")
#  include <libdrm/drm_fourcc.h>
#else
// keep things building without drm_fourcc.h
#  define fourcc_code(a, b, c, d) \
      ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

#  define DRM_FORMAT_RGBA8888 \
      fourcc_code('R', 'A', '2', '4') /* [31:0] R:G:B:A 8:8:8:8 little endian */
#  define DRM_FORMAT_RGB888 fourcc_code('R', 'G', '2', '4') /* [23:0] R:G:B little endian */
#  define DRM_FORMAT_RG88 fourcc_code('R', 'G', '8', '8') /* [15:0] R:G 8:8 little endian */
#  define DRM_FORMAT_ABGR8888 \
      fourcc_code('A', 'B', '2', '4') /* [31:0] A:B:G:R 8:8:8:8 little endian */
#  define DRM_FORMAT_BGR888 fourcc_code('B', 'G', '2', '4') /* [23:0] B:G:R little endian */
#  define DRM_FORMAT_GR88 fourcc_code('G', 'R', '8', '8') /* [15:0] G:R 8:8 little endian */
#  define DRM_FORMAT_R8 fourcc_code('R', '8', ' ', ' ') /* [7:0] R */
#  define DRM_FORMAT_R16 fourcc_code('R', '1', '6', ' ') /* [15:0] R little endian */
#  define DRM_FORMAT_RGB565 fourcc_code('R', 'G', '1', '6') /* [15:0] R:G:B 5:6:5 little endian */
#  define DRM_FORMAT_RG1616 fourcc_code('R', 'G', '3', '2') /* [31:0] R:G 16:16 little endian */
#  define DRM_FORMAT_GR1616 fourcc_code('G', 'R', '3', '2') /* [31:0] G:R 16:16 little endian */
#  define DRM_FORMAT_BGRA1010102 \
      fourcc_code('B', 'A', '3', '0') /* [31:0] B:G:R:A 10:10:10:2 little endian */
#endif

extern "C" {
#include <libavutil/hwcontext_drm.h>
}

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <unistd.h>

#include <qloggingcategory.h>

QT_BEGIN_NAMESPACE

Q_STATIC_LOGGING_CATEGORY(qLHWAccelDRM, "qt.multimedia.ffmpeg.hwacceldrm");

namespace QFFmpeg {

static const quint32 *fourccFromPixelFormat(const QVideoFrameFormat::PixelFormat format)
{
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
    const quint32 rgba_fourcc = DRM_FORMAT_ABGR8888;
    const quint32 rg_fourcc = DRM_FORMAT_GR88;
    const quint32 rg16_fourcc = DRM_FORMAT_GR1616;
#else
    const quint32 rgba_fourcc = DRM_FORMAT_RGBA8888;
    const quint32 rg_fourcc = DRM_FORMAT_RG88;
    const quint32 rg16_fourcc = DRM_FORMAT_RG1616;
#endif

    //    qCDebug(qLHWAccelDRM) << "Getting DRM fourcc for pixel format" << format;

    switch (format) {
    case QVideoFrameFormat::Format_Invalid:
    case QVideoFrameFormat::Format_IMC1:
    case QVideoFrameFormat::Format_IMC2:
    case QVideoFrameFormat::Format_IMC3:
    case QVideoFrameFormat::Format_IMC4:
    case QVideoFrameFormat::Format_SamplerExternalOES:
    case QVideoFrameFormat::Format_Jpeg:
    case QVideoFrameFormat::Format_SamplerRect:
        return nullptr;

    case QVideoFrameFormat::Format_ARGB8888:
    case QVideoFrameFormat::Format_ARGB8888_Premultiplied:
    case QVideoFrameFormat::Format_XRGB8888:
    case QVideoFrameFormat::Format_BGRA8888:
    case QVideoFrameFormat::Format_BGRA8888_Premultiplied:
    case QVideoFrameFormat::Format_BGRX8888:
    case QVideoFrameFormat::Format_ABGR8888:
    case QVideoFrameFormat::Format_XBGR8888:
    case QVideoFrameFormat::Format_RGBA8888:
    case QVideoFrameFormat::Format_RGBX8888:
    case QVideoFrameFormat::Format_AYUV:
    case QVideoFrameFormat::Format_AYUV_Premultiplied:
    case QVideoFrameFormat::Format_UYVY:
    case QVideoFrameFormat::Format_YUYV: {
        static constexpr quint32 format[] = { rgba_fourcc, 0, 0, 0 };
        return format;
    }

    case QVideoFrameFormat::Format_Y8: {
        static constexpr quint32 format[] = { DRM_FORMAT_R8, 0, 0, 0 };
        return format;
    }
    case QVideoFrameFormat::Format_Y16: {
        static constexpr quint32 format[] = { DRM_FORMAT_R16, 0, 0, 0 };
        return format;
    }

    case QVideoFrameFormat::Format_YUV420P:
    case QVideoFrameFormat::Format_YUV422P:
    case QVideoFrameFormat::Format_YV12: {
        static constexpr quint32 format[] = { DRM_FORMAT_R8, DRM_FORMAT_R8, DRM_FORMAT_R8, 0 };
        return format;
    }
    case QVideoFrameFormat::Format_YUV420P10: {
        static constexpr quint32 format[] = { DRM_FORMAT_R16, DRM_FORMAT_R16, DRM_FORMAT_R16, 0 };
        return format;
    }

    case QVideoFrameFormat::Format_NV12:
    case QVideoFrameFormat::Format_NV21: {
        static constexpr quint32 format[] = { DRM_FORMAT_R8, rg_fourcc, 0, 0 };
        return format;
    }

    case QVideoFrameFormat::Format_P010:
    case QVideoFrameFormat::Format_P016: {
        static constexpr quint32 format[] = { DRM_FORMAT_R16, rg16_fourcc, 0, 0 };
        return format;
    }
    }
    return nullptr;
}

namespace {
class DRMTextureHandles : public QVideoFrameTexturesHandles
{
public:
    ~DRMTextureHandles() override;
    quint64 textureHandle(QRhi &, int plane) override { return textures[plane]; }

    TextureConverterBackendPtr
            parentConverterBackend; // ensures the backend is deleted after the texture
    QRhi *rhi = nullptr;
    QOpenGLContext *glContext = nullptr;
    int nPlanes = 0;
    GLuint textures[4] = {};
};
} // namespace

DRMTextureConverter::DRMTextureConverter(QRhi *rhi) : TextureConverterBackend(nullptr)
{
    qCDebug(qLHWAccelDRM) << ">>>> Creating DRM HW accelerator";

    if (!rhi || rhi->backend() != QRhi::OpenGLES2) {
        qWarning() << "DRMTextureConverter: No rhi or non openGL based RHI";
        this->rhi = nullptr;
        return;
    }

    auto *nativeHandles = static_cast<const QRhiGles2NativeHandles *>(rhi->nativeHandles());
    glContext = nativeHandles->context;
    if (!glContext) {
        qCDebug(qLHWAccelDRM) << "    no GL context, disabling";
        return;
    }
    const QString platform = QGuiApplication::platformName();
    QPlatformNativeInterface *pni = QGuiApplication::platformNativeInterface();
    eglDisplay = pni->nativeResourceForIntegration(QByteArrayLiteral("egldisplay"));
    qCDebug(qLHWAccelDRM) << "     platform is" << platform << eglDisplay;

    if (!eglDisplay) {
        qCDebug(qLHWAccelDRM) << "    no egl display, disabling";
        return;
    }
    eglImageTargetTexture2D = eglGetProcAddress("glEGLImageTargetTexture2DOES");
    if (!eglDisplay) {
        qCDebug(qLHWAccelDRM) << "    no eglImageTargetTexture2D, disabling";
        return;
    }

    // everything ok, indicate that we can do zero copy
    this->rhi = rhi;
}

DRMTextureConverter::~DRMTextureConverter() = default;

QVideoFrameTexturesHandlesUPtr
DRMTextureConverter::createTextureHandles(AVFrame *frame,
                                          QVideoFrameTexturesHandlesUPtr /*oldHandles*/)
{
    if (frame->format != AV_PIX_FMT_DRM_PRIME || !eglDisplay) {
        qCDebug(qLHWAccelDRM) << "format/egl error" << frame->format << eglDisplay;
        return nullptr;
    }

    if (!frame->hw_frames_ctx)
        return nullptr;

    AVHWFramesContext *frames = (AVHWFramesContext *)(frame->hw_frames_ctx ? frame->hw_frames_ctx->data : NULL);
    const AVDRMFrameDescriptor *prime = (const AVDRMFrameDescriptor *)frame->data[0];

    // Make sure all fd's in 'prime' are closed when we return from this function
    // QScopeGuard closeObjectsGuard([&prime]() {
    //     for (uint32_t i = 0; i < prime->nb_objects; ++i)
    //         close(prime->objects[i].fd);
    // });

    QOpenGLFunctions functions(glContext);

    AVPixelFormat fmt = HWAccel::format(frame);
    bool needsConversion;
    auto qtFormat = QFFmpegVideoBuffer::toQtPixelFormat(fmt, &needsConversion);
    auto *drm_formats = fourccFromPixelFormat(qtFormat);
    if (!drm_formats || needsConversion) {
        qWarning() << "can't use DMA transfer for pixel format" << fmt << qtFormat;
        return nullptr;
    }

    auto *desc = QVideoTextureHelper::textureDescription(qtFormat);
    int nPlanes = 0;
    for (; nPlanes < 5; ++nPlanes) {
        if (drm_formats[nPlanes] == 0)
            break;
    }
    Q_ASSERT(nPlanes == desc->nplanes);
    nPlanes = desc->nplanes;

    rhi->makeThreadLocalNativeContextCurrent();

    EGLImage images[4];
    GLuint glTextures[4] = {};
    functions.glGenTextures(nPlanes, glTextures);
    for (int i = 0; i < nPlanes; ++i) {

#define LAYER 0
#define PLANE i

        QSize planeSize = desc->rhiPlaneSize(QSize(frame->width, frame->height), i, rhi);
        constexpr uint32_t maxAttrCount = 18;
        EGLAttrib img_attr[maxAttrCount] = {
            EGL_LINUX_DRM_FOURCC_EXT,
            (EGLint)drm_formats[i],
            EGL_WIDTH,
            (frames ? frames->width : frame->width) / (i + 1),
            EGL_HEIGHT,
            (frames ? frames->height : frame->height) / (i + 1),
            EGL_DMA_BUF_PLANE0_FD_EXT,
            prime->objects[prime->layers[LAYER].planes[PLANE].object_index].fd,
            EGL_DMA_BUF_PLANE0_OFFSET_EXT,
            (EGLint)prime->layers[LAYER].planes[PLANE].offset,
            EGL_DMA_BUF_PLANE0_PITCH_EXT,
            (EGLint)prime->layers[LAYER].planes[PLANE].pitch,
        };
        uint32_t img_attr_idx = 12;
        uint64_t modifier =
                prime->objects[prime->layers[LAYER].planes[PLANE].object_index].format_modifier;
        if (modifier != DRM_FORMAT_MOD_INVALID) {
            img_attr[img_attr_idx++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
            img_attr[img_attr_idx++] = modifier & 0xFFFFFFFF;
            img_attr[img_attr_idx++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
            img_attr[img_attr_idx++] = modifier >> 32;
        }
        img_attr[img_attr_idx++] = EGL_NONE;
        Q_ASSERT(img_attr_idx <= maxAttrCount);
        images[i] = eglCreateImage(eglDisplay, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr,
                                   img_attr);
        if (!images[i]) {
            const GLenum error = eglGetError();
            if (error == EGL_BAD_MATCH) {
                qWarning() << "eglCreateImage failed for plane" << i
                           << "with error code EGL_BAD_MATCH, "
                              "disabling hardware acceleration. This could indicate an EGL "
                              "implementation issue."
                           << "\nEGL vendor:" << eglQueryString(eglDisplay, EGL_VENDOR);
                this->rhi = nullptr; // Disabling texture conversion here to fix QTBUG-112312
                return nullptr;
            }
            if (error) {
                qWarning() << "eglCreateImage failed for plane" << i << "with error code" << error;
                return nullptr;
            }
        }
        functions.glActiveTexture(GL_TEXTURE0 + i);
        functions.glBindTexture(GL_TEXTURE_2D, glTextures[i]);

        PFNGLEGLIMAGETARGETTEXTURE2DOESPROC eglImageTargetTexture2D =
                (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)this->eglImageTargetTexture2D;
        eglImageTargetTexture2D(GL_TEXTURE_2D, images[i]);
        GLenum error = glGetError();
        if (error)
            qWarning() << "eglImageTargetTexture2D failed with error code" << error;
    }

    for (int i = 0; i < nPlanes; ++i) {
        functions.glActiveTexture(GL_TEXTURE0 + i);
        functions.glBindTexture(GL_TEXTURE_2D, 0);
        eglDestroyImage(eglDisplay, images[i]);
    }

    auto textureHandles = std::make_unique<DRMTextureHandles>();
    textureHandles->parentConverterBackend = shared_from_this();
    textureHandles->nPlanes = nPlanes;
    textureHandles->rhi = rhi;
    textureHandles->glContext = glContext;

    for (int i = 0; i < 4; ++i)
        textureHandles->textures[i] = glTextures[i];

    return textureHandles;
}

DRMTextureHandles::~DRMTextureHandles()
{
    if (rhi) {
        rhi->makeThreadLocalNativeContextCurrent();
        QOpenGLFunctions functions(glContext);
        functions.glDeleteTextures(nPlanes, textures);
    }
}

} // namespace QFFmpeg

QT_END_NAMESPACE
