#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <cstdint>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "modules/config_module.hpp"

namespace nuc_display::core { class Renderer; }

namespace nuc_display::modules {

class CameraModule {
public:
    CameraModule();
    ~CameraModule();

    // Open camera (from config — handles auto-detect if device is empty)
    bool open(const CameraConfig& config);
    void close();
    bool is_open() const;
    
    // Capture the latest frame from V4L2. Returns false if camera disconnected.
    bool capture_frame();
    
    // Render latest frame as EGLImage / texture
    void render(core::Renderer& renderer, EGLDisplay egl_display,
                float src_x, float src_y, float src_w, float src_h,
                float x, float y, float w, float h);
    
    // Info
    std::string device_path() const { return device_path_; }
    std::string device_name() const { return device_name_; }

private:
    // V4L2 setup
    bool init_v4l2(const std::string& device, int w, int h, int fps, uint32_t fourcc);
    bool start_streaming();
    void stop_streaming();
    void cleanup_v4l2();
    
    // Device discovery
    static std::string find_camera_device();
    static bool is_capture_device(const std::string& path);
    
    // Buffer management
    struct V4L2Buffer {
        void* start = nullptr;
        size_t length = 0;
        int dmabuf_fd = -1;
    };
    
    // EGL/GL setup (called once on first render)
    void init_gl(core::Renderer& renderer, EGLDisplay egl_display);
    
    // V4L2 state
    int v4l2_fd_ = -1;
    std::vector<V4L2Buffer> buffers_;
    bool streaming_ = false;
    bool use_dmabuf_ = false;        // True if VIDIOC_EXPBUF succeeded (zero-copy)
    uint32_t capture_fourcc_ = 0;
    int capture_width_ = 0;
    int capture_height_ = 0;
    std::string device_path_;
    std::string device_name_;
    
    // Frame state
    bool has_frame_ = false;
    int current_buf_index_ = -1;     // Currently dequeued buffer index for DMA-BUF
    int timeout_consecutive_ = 0;    // Tracks wedged camera state
    
    // EGL/GL state (same pattern as VideoDecoder)
    EGLDisplay egl_display_ = EGL_NO_DISPLAY;
    EGLImageKHR current_egl_image_ = EGL_NO_IMAGE_KHR;
    GLuint texture_id_ = 0;
    GLuint program_ = 0;
    GLint pos_loc_ = -1;
    GLint tex_coord_loc_ = -1;
    GLint sampler_loc_ = -1;
    bool gl_initialized_ = false;
    
    // Software fallback texture (when DMA-BUF not available, e.g. MJPG → RGB decode)
    bool sw_upload_ = false;
    GLuint sw_texture_id_ = 0;
    std::vector<uint8_t> rgb_buffer_;  // Decoded RGB data for software path

    // EGL function pointers (cached)
    PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR_ = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR_ = nullptr;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES_ = nullptr;
    
    static constexpr int NUM_BUFFERS = 4;
};

} // namespace nuc_display::modules
