#include "display_manager.hpp"

#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>

namespace nuc_display::core {

std::string error_to_string(DisplayError err) {
    switch (err) {
        case DisplayError::DrmOpenFailed: return "DrmOpenFailed";
        case DisplayError::DrmResourcesFailed: return "DrmResourcesFailed";
        case DisplayError::DrmConnectorFailed: return "DrmConnectorFailed";
        case DisplayError::DrmEncoderFailed: return "DrmEncoderFailed";
        case DisplayError::DrmCrtcFailed: return "DrmCrtcFailed";
        case DisplayError::GbmDeviceFailed: return "GbmDeviceFailed";
        case DisplayError::GbmSurfaceFailed: return "GbmSurfaceFailed";
        case DisplayError::EglDisplayFailed: return "EglDisplayFailed";
        case DisplayError::EglInitializeFailed: return "EglInitializeFailed";
        case DisplayError::EglConfigFailed: return "EglConfigFailed";
        case DisplayError::EglContextFailed: return "EglContextFailed";
        case DisplayError::EglSurfaceFailed: return "EglSurfaceFailed";
        default: return "Unknown Error";
    }
}

std::expected<std::unique_ptr<DisplayManager>, DisplayError> DisplayManager::create() {
    auto dm = std::unique_ptr<DisplayManager>(new DisplayManager());
    
    if (auto res = dm->init_drm(); !res) return std::unexpected(res.error());
    if (auto res = dm->init_gbm(); !res) return std::unexpected(res.error());
    if (auto res = dm->init_egl(); !res) return std::unexpected(res.error());
    
    return std::move(dm);
}

DisplayManager::~DisplayManager() {
    shutdown_display();

    // Clean up EGL
    if (egl_display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_surface_ != EGL_NO_SURFACE) eglDestroySurface(egl_display_, egl_surface_);
        if (egl_context_ != EGL_NO_CONTEXT) eglDestroyContext(egl_display_, egl_context_);
        eglTerminate(egl_display_);
    }

    // Clean up GBM
    if (previous_bo_) {
        drmModeRmFB(drm_fd_, previous_fb_);
        gbm_surface_release_buffer(gbm_surface_, previous_bo_);
    }
    if (gbm_surface_) gbm_surface_destroy(gbm_surface_);
    if (gbm_dev_) gbm_device_destroy(gbm_dev_);

    // Clean up DRM
    if (drm_encoder_) drmModeFreeEncoder(drm_encoder_);
    if (drm_connector_) drmModeFreeConnector(drm_connector_);
    if (drm_resources_) drmModeFreeResources(drm_resources_);
    if (drm_fd_ >= 0) close(drm_fd_);
}

void DisplayManager::shutdown_display() {
    // Clear the screen by setting the CRTC back to nothing
    if (drm_fd_ >= 0 && crtc_id_ > 0) {
        drmModeSetCrtc(drm_fd_, crtc_id_, 0, 0, 0, nullptr, 0, nullptr);
    }
}

std::expected<void, DisplayError> DisplayManager::init_drm() {
    for (int i = 0; i < 10; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/dri/card%d", i);
        drm_fd_ = open(path, O_RDWR | O_CLOEXEC);
        if (drm_fd_ >= 0) {
            drm_resources_ = drmModeGetResources(drm_fd_);
            if (drm_resources_ && drm_resources_->count_connectors > 0) {
                std::cout << "Successfully opened " << path << std::endl;
                break;
            }
            if (drm_resources_) {
                drmModeFreeResources(drm_resources_);
                drm_resources_ = nullptr;
            }
            close(drm_fd_);
            drm_fd_ = -1;
        }
    }

    if (drm_fd_ < 0 || !drm_resources_) {
        return std::unexpected(DisplayError::DrmOpenFailed);
    }

    bool connected = false;
    for (int i = 0; i < drm_resources_->count_connectors; i++) {
        drmModeConnector *conn = drmModeGetConnector(drm_fd_, drm_resources_->connectors[i]);
        if (!conn) continue;

        if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
            drm_connector_ = conn;
            mode_ = conn->modes[0]; // Highest res
            connected = true;
            break;
        }
        drmModeFreeConnector(conn);
    }

    if (!connected) return std::unexpected(DisplayError::DrmConnectorFailed);

    std::cout << "Found Display! " << mode_.hdisplay << "x" << mode_.vdisplay << std::endl;

    drm_encoder_ = drmModeGetEncoder(drm_fd_, drm_connector_->encoders[0]);
    if (!drm_encoder_) return std::unexpected(DisplayError::DrmEncoderFailed);

    crtc_id_ = drm_encoder_->crtc_id;
    if (!crtc_id_) {
        for (int i = 0; i < drm_resources_->count_crtcs; i++) {
            if (drm_encoder_->possible_crtcs & (1 << i)) {
                crtc_id_ = drm_resources_->crtcs[i];
                break;
            }
        }
    }
    if (!crtc_id_) return std::unexpected(DisplayError::DrmCrtcFailed);

    return {};
}

std::expected<void, DisplayError> DisplayManager::init_gbm() {
    gbm_dev_ = gbm_create_device(drm_fd_);
    if (!gbm_dev_) return std::unexpected(DisplayError::GbmDeviceFailed);

    gbm_surface_ = gbm_surface_create(gbm_dev_,
                                      mode_.hdisplay,
                                      mode_.vdisplay,
                                      GBM_FORMAT_XRGB8888,
                                      GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!gbm_surface_) return std::unexpected(DisplayError::GbmSurfaceFailed);

    return {};
}

std::expected<void, DisplayError> DisplayManager::init_egl() {
    EGLint major, minor;
    egl_display_ = eglGetDisplay((EGLNativeDisplayType)gbm_dev_);
    if (egl_display_ == EGL_NO_DISPLAY) return std::unexpected(DisplayError::EglDisplayFailed);

    if (!eglInitialize(egl_display_, &major, &minor)) return std::unexpected(DisplayError::EglInitializeFailed);

    eglBindAPI(EGL_OPENGL_ES_API);

    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    EGLint num_configs;
    eglChooseConfig(egl_display_, config_attribs, nullptr, 0, &num_configs);
    if (num_configs == 0) return std::unexpected(DisplayError::EglConfigFailed);

    std::vector<EGLConfig> configs(num_configs);
    eglChooseConfig(egl_display_, config_attribs, configs.data(), num_configs, &num_configs);
    
    bool found_config = false;
    for (int i = 0; i < num_configs; i++) {
        EGLint visual_id;
        if (eglGetConfigAttrib(egl_display_, configs[i], EGL_NATIVE_VISUAL_ID, &visual_id)) {
            if (visual_id == GBM_FORMAT_XRGB8888) {
                egl_config_ = configs[i];
                found_config = true;
                break;
            }
        }
    }
    
    if (!found_config) {
        std::cerr << "Falling back to first available EGL config\n";
        egl_config_ = configs[0];
    }

    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    egl_context_ = eglCreateContext(egl_display_, egl_config_, EGL_NO_CONTEXT, context_attribs);
    if (egl_context_ == EGL_NO_CONTEXT) return std::unexpected(DisplayError::EglContextFailed);

    egl_surface_ = eglCreateWindowSurface(egl_display_, egl_config_, (EGLNativeWindowType)gbm_surface_, nullptr);
    if (egl_surface_ == EGL_NO_SURFACE) return std::unexpected(DisplayError::EglSurfaceFailed);

    eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, egl_context_);

    return {};
}

void DisplayManager::page_flip_handler(int fd, unsigned int frame, unsigned int sec, unsigned int usec, void *data) {
    auto dm = static_cast<DisplayManager*>(data);
    dm->waiting_for_flip_ = false;
    
    if (dm->previous_bo_) {
        drmModeRmFB(fd, dm->previous_fb_);
        gbm_surface_release_buffer(dm->gbm_surface_, dm->previous_bo_);
    }
}

void DisplayManager::swap_buffers() {
    eglSwapBuffers(egl_display_, egl_surface_);
}

bool DisplayManager::page_flip() {
    struct gbm_bo* bo = gbm_surface_lock_front_buffer(gbm_surface_);
    if (!bo) {
        std::cerr << "Failed to lock front buffer\n";
        return false;
    }
    
    uint32_t handle = gbm_bo_get_handle(bo).u32;
    uint32_t pitch = gbm_bo_get_stride(bo);
    
    uint32_t fb;
    if (drmModeAddFB(drm_fd_, mode_.hdisplay, mode_.vdisplay, 24, 32, pitch, handle, &fb)) {
        std::cerr << "Failed to create DRM Framebuffer\n";
        return false;
    }

    if (!previous_bo_) {
        if (drmModeSetCrtc(drm_fd_, crtc_id_, fb, 0, 0, &drm_connector_->connector_id, 1, &mode_)) {
            std::cerr << "Failed to set CRTC\n";
            return false;
        }
    } else {
        if (drmModePageFlip(drm_fd_, crtc_id_, fb, DRM_MODE_PAGE_FLIP_EVENT, this)) {
            std::cerr << "Page flip failed\n";
            return false;
        }
        waiting_for_flip_ = true;
    }

    previous_bo_ = bo;
    previous_fb_ = fb;
    return true;
}

void DisplayManager::process_drm_events(int timeout_ms) {
    drmEventContext evctx = {};
    evctx.version = DRM_EVENT_CONTEXT_VERSION;
    evctx.page_flip_handler = page_flip_handler;

    struct pollfd pfd = { .fd = drm_fd_, .events = POLLIN };
    
    while (waiting_for_flip_) {
        if (poll(&pfd, 1, timeout_ms) > 0) {
            drmHandleEvent(drm_fd_, &evctx);
        } else {
            break; // Timeout or error
        }
    }
}

} // namespace nuc_display::core
