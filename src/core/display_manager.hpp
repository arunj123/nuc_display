#pragma once

#include <string>
#include <vector>
#include <memory>
#include <expected>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

namespace nuc_display::core {

enum class DisplayError {
    DrmOpenFailed,
    DrmResourcesFailed,
    DrmConnectorFailed,
    DrmEncoderFailed,
    DrmCrtcFailed,
    GbmDeviceFailed,
    GbmSurfaceFailed,
    EglDisplayFailed,
    EglInitializeFailed,
    EglConfigFailed,
    EglContextFailed,
    EglSurfaceFailed,
    DrmMasterFailed
};

std::string error_to_string(DisplayError err);

class DisplayManager {
public:
    static std::expected<std::unique_ptr<DisplayManager>, DisplayError> create();
    
    ~DisplayManager();
    
    // Non-copyable, non-movable for safety
    DisplayManager(const DisplayManager&) = delete;
    DisplayManager& operator=(const DisplayManager&) = delete;
    DisplayManager(DisplayManager&&) = delete;
    DisplayManager& operator=(DisplayManager&&) = delete;

    void swap_buffers();
    bool page_flip();
    void process_drm_events(int timeout_ms);
    void shutdown_display();

    // Accessors
    int drm_fd() const { return drm_fd_; }
    uint32_t width() const { return mode_.hdisplay; }
    uint32_t height() const { return mode_.vdisplay; }
    EGLDisplay egl_display() const { return egl_display_; }

private:
    DisplayManager() = default;
    
    std::expected<void, DisplayError> init_drm();
    std::expected<void, DisplayError> init_gbm();
    std::expected<void, DisplayError> init_egl();
    
    static void page_flip_handler(int fd, unsigned int frame, unsigned int sec, unsigned int usec, void *data);

    // DRM State
    int drm_fd_ = -1;
    drmModeRes* drm_resources_ = nullptr;
    drmModeConnector* drm_connector_ = nullptr;
    drmModeEncoder* drm_encoder_ = nullptr;
    drmModeModeInfo mode_{};
    uint32_t crtc_id_ = 0;

    // GBM State
    struct gbm_device* gbm_dev_ = nullptr;
    struct gbm_surface* gbm_surface_ = nullptr;
    
    // Page Flip State
    struct gbm_bo* current_bo_ = nullptr;
    uint32_t current_fb_ = 0;
    struct gbm_bo* next_bo_ = nullptr;
    uint32_t next_fb_ = 0;
    bool waiting_for_flip_ = false;

    // EGL State
    EGLDisplay egl_display_ = EGL_NO_DISPLAY;
    EGLConfig egl_config_ = nullptr;
    EGLContext egl_context_ = EGL_NO_CONTEXT;
    EGLSurface egl_surface_ = EGL_NO_SURFACE;
};

} // namespace nuc_display::core
