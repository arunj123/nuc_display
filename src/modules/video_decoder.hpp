#pragma once

#include "modules/media_module.hpp"
#include <string>
#include <expected>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <va/va.h>
#include <libswscale/swscale.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext_drm.h>
#include <va/va.h>
#include <va/va_drm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <alsa/asoundlib.h>
}

#include "modules/container_reader.hpp"
#include "core/renderer.hpp"

namespace nuc_display::modules {

class VideoDecoder : public MediaModule {
public:
    VideoDecoder();
    ~VideoDecoder() override;

    std::expected<void, MediaError> load(const std::string& filepath) override;
    std::expected<void, MediaError> process(double time_sec) override;

    // VA-API specific initialization
    std::expected<void, MediaError> init_vaapi(int drm_fd);

    // Optional: render the current frame to OpenGL via EGLImage zero-copy
    bool render(core::Renderer& renderer, EGLDisplay egl_display, 
                float src_x, float src_y, float src_w, float src_h,
                float x, float y, float w, float h, double time_sec);

    void rewind_stream();
    void load_playlist(const std::vector<std::string>& files);
    void next_video();
    
    void set_audio_enabled(bool enabled);
    void init_audio(const std::string& device_name = "default");

private:
    void cleanup_codec();
    
    std::vector<std::string> playlist_;
    size_t playlist_index_ = 0;
    
    ContainerReader container_;
    
    // Video State
    int video_stream_index_ = -1;
    AVCodecContext* codec_ctx_ = nullptr;
    AVCodec* codec_ = nullptr;
    AVBufferRef* hw_device_ctx_ = nullptr;
    
    // Audio State
    bool audio_enabled_ = false;
    int audio_stream_index_ = -1;
    snd_pcm_t* pcm_handle_ = nullptr;
    AVCodecContext* audio_codec_ctx_ = nullptr;
    AVFrame* audio_frame_ = nullptr;
    
    AVFrame* hw_frame_ = nullptr;
    AVFrame* drm_frame_ = nullptr;
    
    uint32_t current_texture_id_ = 0;
    EGLImageKHR current_egl_image_ = EGL_NO_IMAGE_KHR;
    EGLDisplay egl_display_ = EGL_NO_DISPLAY;
    
    // Shader components for external OES
    GLuint external_program_ = 0;
    GLuint external_pos_loc_ = 0;
    GLuint external_tex_coord_loc_ = 0;
    GLuint external_sampler_loc_ = 0;
    
    VADisplay va_display_ = nullptr;
    double last_frame_time_ = -1.0;
};

} // namespace nuc_display::modules
