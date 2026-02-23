#pragma once

#include "modules/media_module.hpp"
#include <string>
#include <expected>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <va/va.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
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

#include <deque>
#include <mutex>
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
    
    // Buffering State
    std::deque<AVPacket*> packet_queue_;
    std::deque<AVFrame*> video_frame_queue_;
    std::deque<AVFrame*> audio_frame_queue_;
    const size_t max_packets_ = 100;
    const size_t max_video_frames_ = 4; // Reduced from 8 to prevent hardware surface exhaustion
    const size_t max_audio_frames_ = 20;
    bool eof_reached_ = false;
    
    // Audio State
    bool audio_enabled_ = false;
    int audio_stream_index_ = -1;
    snd_pcm_t* pcm_handle_ = nullptr;
    AVCodecContext* audio_codec_ctx_ = nullptr;
    AVFrame* audio_frame_ = nullptr;
    SwrContext* swr_ctx_ = nullptr;
    std::vector<uint8_t> audio_spillover_;
    
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
    double video_start_time_ = -1.0;
    AVRational stream_timebase_ = {1, 1};
    int frames_rendered_ = 0;
    
    uint32_t negotiated_rate_ = 48000;
    std::mutex queue_mutex_;
    int get_buffer_retry_count_ = 0;
    int decoding_failure_count_ = 0;
    int packets_sent_without_frame_ = 0;
};

} // namespace nuc_display::modules
