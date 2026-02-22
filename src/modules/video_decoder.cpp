#include "modules/video_decoder.hpp"
#include <iostream>

namespace nuc_display::modules {

VideoDecoder::VideoDecoder() {
    this->hw_frame_ = av_frame_alloc();
    this->audio_frame_ = av_frame_alloc();
}

VideoDecoder::~VideoDecoder() {
    if (this->hw_frame_) {
        av_frame_free(&this->hw_frame_);
    }
    if (this->drm_frame_) {
        av_frame_free(&this->drm_frame_);
    }
    if (this->codec_ctx_) {
        avcodec_free_context(&this->codec_ctx_);
    }
    if (this->audio_codec_ctx_) {
        avcodec_free_context(&this->audio_codec_ctx_);
    }
    if (this->hw_device_ctx_) {
        av_buffer_unref(&this->hw_device_ctx_);
    }
    if (this->audio_frame_) {
        av_frame_free(&this->audio_frame_);
    }
    if (this->pcm_handle_) {
        snd_pcm_close(this->pcm_handle_);
    }
}

std::expected<void, MediaError> VideoDecoder::init_vaapi(int drm_fd) {
    (void)drm_fd;
    std::cout << "Initializing FFmpeg VA-API Hardware Device Context\n";
    
    // Pass the DRM device path directly to FFmpeg to create the HW device context
    int err = av_hwdevice_ctx_create(&this->hw_device_ctx_, AV_HWDEVICE_TYPE_VAAPI, "/dev/dri/renderD128", nullptr, 0);
    if (err < 0) {
        std::cerr << "Failed to create VA-API HW device context. Falling back to software.\n";
        // Not a fatal error, FFmpeg can fallback to software decoding
        return {}; 
    }
    
    std::cout << "VA-API HW device context initialized successfully.\n";
    return {};
}

void VideoDecoder::rewind_stream() {
    this->container_.rewind();
    if (this->codec_ctx_) {
        avcodec_flush_buffers(this->codec_ctx_);
    }
    if (this->audio_codec_ctx_) {
        avcodec_flush_buffers(this->audio_codec_ctx_);
    }
    this->last_frame_time_ = -1.0;
}

void VideoDecoder::cleanup_codec() {
    if (this->codec_ctx_) {
        avcodec_free_context(&this->codec_ctx_);
        this->codec_ctx_ = nullptr;
    }
    if (this->audio_codec_ctx_) {
        avcodec_free_context(&this->audio_codec_ctx_);
        this->audio_codec_ctx_ = nullptr;
    }
    this->codec_ = nullptr;
    this->video_stream_index_ = -1;
    this->audio_stream_index_ = -1;
    this->last_frame_time_ = -1.0;
}

void VideoDecoder::load_playlist(const std::vector<std::string>& files) {
    if (files.empty()) return;
    this->playlist_ = files;
    this->playlist_index_ = 0;
    this->load(this->playlist_[this->playlist_index_]);
}

void VideoDecoder::next_video() {
    if (this->playlist_.empty()) return;
    this->playlist_index_ = (this->playlist_index_ + 1) % this->playlist_.size();
    this->load(this->playlist_[this->playlist_index_]);
}

std::expected<void, MediaError> VideoDecoder::load(const std::string& filepath) {
    std::cout << "VideoDecoder: Loading " << filepath << "\n";
    this->cleanup_codec();
    
    auto open_res = this->container_.open(filepath);
    if (!open_res) return open_res;

    this->video_stream_index_ = this->container_.find_video_stream();
    if (this->video_stream_index_ < 0) {
        return std::unexpected(MediaError::UnsupportedFormat);
    }

    AVCodecParameters* codec_params = this->container_.get_codec_params(this->video_stream_index_);
    this->codec_ = const_cast<AVCodec*>(avcodec_find_decoder(codec_params->codec_id));
    
    if (this->codec_) {
        this->codec_ctx_ = avcodec_alloc_context3(this->codec_);
        avcodec_parameters_to_context(this->codec_ctx_, codec_params);
        
        // Setup hardware decoding if context is available
        if (this->hw_device_ctx_) {
            this->codec_ctx_->hw_device_ctx = av_buffer_ref(this->hw_device_ctx_);
        }
        
        if (avcodec_open2(this->codec_ctx_, this->codec_, nullptr) < 0) {
            return std::unexpected(MediaError::DecodeFailed);
        }
    } else {
        return std::unexpected(MediaError::UnsupportedFormat);
    }
    
    // Setup audio decoder if enabled
    if (this->audio_enabled_) {
        this->audio_stream_index_ = this->container_.find_audio_stream();
        if (this->audio_stream_index_ >= 0) {
            AVCodecParameters* a_params = this->container_.get_codec_params(this->audio_stream_index_);
            const AVCodec* a_codec = avcodec_find_decoder(a_params->codec_id);
            if (a_codec) {
                this->audio_codec_ctx_ = avcodec_alloc_context3(a_codec);
                avcodec_parameters_to_context(this->audio_codec_ctx_, a_params);
                if (avcodec_open2(this->audio_codec_ctx_, a_codec, nullptr) == 0 && this->pcm_handle_) {
                    std::cout << "VideoDecoder: Initializing ALSA PCM for " << a_params->sample_rate << "Hz\n";
                    int channels = a_params->ch_layout.nb_channels > 0 ? a_params->ch_layout.nb_channels : 2;
                    // Try to configure ALSA with the stream's sample rate and channels
                    snd_pcm_hw_params_t *params;
                    snd_pcm_hw_params_alloca(&params);
                    snd_pcm_hw_params_any(this->pcm_handle_, params);
                    snd_pcm_hw_params_set_access(this->pcm_handle_, params, SND_PCM_ACCESS_RW_INTERLEAVED);
                    int dir = 0;
                    if (this->audio_codec_ctx_->sample_fmt == AV_SAMPLE_FMT_FLTP || this->audio_codec_ctx_->sample_fmt == AV_SAMPLE_FMT_FLT) {
                        snd_pcm_hw_params_set_format(this->pcm_handle_, params, SND_PCM_FORMAT_FLOAT_LE);
                    } else if (this->audio_codec_ctx_->sample_fmt == AV_SAMPLE_FMT_S32P || this->audio_codec_ctx_->sample_fmt == AV_SAMPLE_FMT_S32) {
                        snd_pcm_hw_params_set_format(this->pcm_handle_, params, SND_PCM_FORMAT_S32_LE);
                    } else {
                        snd_pcm_hw_params_set_format(this->pcm_handle_, params, SND_PCM_FORMAT_S16_LE); // fallback
                    }
                    snd_pcm_hw_params_set_channels(this->pcm_handle_, params, channels);
                    unsigned int rate = a_params->sample_rate;
                    snd_pcm_hw_params_set_rate_near(this->pcm_handle_, params, &rate, &dir);
                    snd_pcm_hw_params(this->pcm_handle_, params);
                }
            }
        }
    }
    
    std::cout << "VideoDecoder: Codec opened.\n";
    return {};
}

void VideoDecoder::set_audio_enabled(bool enabled) {
    this->audio_enabled_ = enabled;
}

void VideoDecoder::init_audio(const std::string& device_name) {
    std::cout << "Initializing ALSA VideoDecoder Audio Device: " << device_name << "\n";
    int err = snd_pcm_open(&this->pcm_handle_, device_name.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        std::cerr << "ALSA: Cannot open audio device: " << snd_strerror(err) << "\n";
        this->pcm_handle_ = nullptr;
    }
}

std::expected<void, MediaError> VideoDecoder::process(double time_sec) {
    (void)time_sec;
    return {};
}

bool VideoDecoder::render(core::Renderer& renderer, EGLDisplay egl_display, 
                          float src_x, float src_y, float src_w, float src_h,
                          float x, float y, float w, float h, double time_sec) {
    if (!this->codec_ctx_) return false;
    
    // 1. Initialize EGL Extension Pointers and Shader (Once)
    if (this->external_program_ == 0) {
        this->egl_display_ = egl_display;
        
        const char* vs = R"(
            attribute vec4 a_position;
            attribute vec2 a_texCoord;
            varying vec2 v_texCoord;
            void main() {
                gl_Position = a_position;
                v_texCoord = a_texCoord;
            }
        )";
        // Require the external OES extension
        const char* fs = R"(
            #extension GL_OES_EGL_image_external : require
            precision mediump float;
            varying vec2 v_texCoord;
            uniform samplerExternalOES s_texture;
            void main() {
                // Hardware NV12 YUV is often sampled as raw data if EGL cannot imply color space. 
                // We perform limited-range BT.601 YUV to RGB conversion.
                vec4 texel = texture2D(s_texture, v_texCoord);
                
                // For many DRM PRIME backends, the "R" channel is Y, "G" is U, "B" is V.
                // However, depending on the driver, it may sample as RGB or YUV directly.
                // Let's assume standard BT.601 matrix for YCbCr if it's sampling raw YUV components:
                float y = texel.r - 0.0627;
                float u = texel.g - 0.5;
                float v = texel.b - 0.5;
                
                float r = y + 1.402 * v;
                float g = y - 0.3441 * u - 0.7141 * v;
                float b = y + 1.772 * u;
                
                gl_FragColor = vec4(r, g, b, 1.0);
            }
        )";
        
        GLuint vs_id = renderer.compile_shader(GL_VERTEX_SHADER, vs);
        GLuint fs_id = renderer.compile_shader(GL_FRAGMENT_SHADER, fs);
        this->external_program_ = renderer.link_program(vs_id, fs_id);
        glDeleteShader(vs_id);
        glDeleteShader(fs_id);
        
        this->external_pos_loc_ = glGetAttribLocation(this->external_program_, "a_position");
        this->external_tex_coord_loc_ = glGetAttribLocation(this->external_program_, "a_texCoord");
        this->external_sampler_loc_ = glGetUniformLocation(this->external_program_, "s_texture");
        
        glGenTextures(1, &this->current_texture_id_);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, this->current_texture_id_);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    
    // 2. Fetch Next Frame if Needed
    bool frame_ready = false;
    bool eof_reached = false;
    
    // Pacing at ~30fps (simplified)
    if (this->last_frame_time_ < 0.0 || (time_sec - this->last_frame_time_) >= 0.033) {
        int ret = avcodec_receive_frame(this->codec_ctx_, this->hw_frame_);
        if (ret == 0) {
            frame_ready = true;
        } else if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            int max_packets_per_frame = 20; // Prevent blocking the main loop indefinitely
            while (!frame_ready && max_packets_per_frame-- > 0) {
                auto packet_res = this->container_.read_packet();
                if (!packet_res) {
                    eof_reached = true;
                    break;
                }
                AVPacket* packet = packet_res.value();
                if (packet->stream_index == this->video_stream_index_) {
                    if (avcodec_send_packet(this->codec_ctx_, packet) < 0) {
                        eof_reached = true;
                        break;
                    }
                    ret = avcodec_receive_frame(this->codec_ctx_, this->hw_frame_);
                    if (ret == 0) frame_ready = true;
                } else if (this->audio_enabled_ && packet->stream_index == this->audio_stream_index_ && this->audio_codec_ctx_) {
                    if (avcodec_send_packet(this->audio_codec_ctx_, packet) == 0) {
                        while (avcodec_receive_frame(this->audio_codec_ctx_, this->audio_frame_) >= 0) {
                            if (this->pcm_handle_) {
                                // Raw interleaved audio dump to ALSA. 
                                // (If stream is planar FLTP, only the L channel will write properly without libswresample, but it gets the job done for now)
                                snd_pcm_writei(this->pcm_handle_, this->audio_frame_->data[0], this->audio_frame_->nb_samples);
                            }
                        }
                    }
                }
            }
        }
    }
    
    // 3. Map Frame to DMA-BUF and Create EGLImage (Zero-Copy)
    if (frame_ready) {
        if (!this->drm_frame_) this->drm_frame_ = av_frame_alloc();
        
        this->drm_frame_->format = AV_PIX_FMT_DRM_PRIME;
        if (av_hwframe_map(this->drm_frame_, this->hw_frame_, AV_HWFRAME_MAP_READ) == 0) {
            AVDRMFrameDescriptor* desc = (AVDRMFrameDescriptor*)this->drm_frame_->data[0];
            
            PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR_ptr = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
            PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR_ptr = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
            PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES_ptr = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
            
            if (eglCreateImageKHR_ptr && glEGLImageTargetTexture2DOES_ptr && desc && desc->nb_layers > 0) {
                if (this->current_egl_image_ != EGL_NO_IMAGE_KHR) {
                    eglDestroyImageKHR_ptr(this->egl_display_, this->current_egl_image_);
                    this->current_egl_image_ = EGL_NO_IMAGE_KHR;
                }
                
                std::vector<EGLint> attribs;
                attribs.push_back(EGL_WIDTH); attribs.push_back(this->codec_ctx_->width);
                attribs.push_back(EGL_HEIGHT); attribs.push_back(this->codec_ctx_->height);
                attribs.push_back(EGL_LINUX_DRM_FOURCC_EXT); attribs.push_back(desc->layers[0].format);
                
                // Add plane 0 (Y)
                attribs.push_back(EGL_DMA_BUF_PLANE0_FD_EXT); attribs.push_back(desc->objects[desc->layers[0].planes[0].object_index].fd);
                attribs.push_back(EGL_DMA_BUF_PLANE0_OFFSET_EXT); attribs.push_back(desc->layers[0].planes[0].offset);
                attribs.push_back(EGL_DMA_BUF_PLANE0_PITCH_EXT); attribs.push_back(desc->layers[0].planes[0].pitch);
                
                // Add plane 1 (UV) if it exists (NV12)
                if (desc->layers[0].nb_planes > 1) {
                    attribs.push_back(EGL_DMA_BUF_PLANE1_FD_EXT); attribs.push_back(desc->objects[desc->layers[0].planes[1].object_index].fd);
                    attribs.push_back(EGL_DMA_BUF_PLANE1_OFFSET_EXT); attribs.push_back(desc->layers[0].planes[1].offset);
                    attribs.push_back(EGL_DMA_BUF_PLANE1_PITCH_EXT); attribs.push_back(desc->layers[0].planes[1].pitch);
                }
                
                attribs.push_back(EGL_NONE);
                
                this->current_egl_image_ = eglCreateImageKHR_ptr(
                    this->egl_display_, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attribs.data()
                );
                
                if (this->current_egl_image_ != EGL_NO_IMAGE_KHR) {
                    glBindTexture(GL_TEXTURE_EXTERNAL_OES, this->current_texture_id_);
                    glEGLImageTargetTexture2DOES_ptr(GL_TEXTURE_EXTERNAL_OES, this->current_egl_image_);
                } else {
                    std::cerr << "VideoDecoder: Failed to create EGLImageKHR from DMA-BUF.\n";
                }
            } else {
                std::cerr << "VideoDecoder: EGL extensions missing or bad desc.\n";
            }
            av_frame_unref(this->drm_frame_);
        } else {
            std::cerr << "VideoDecoder: Failed to map VAAPI frame to DRM PRIME.\n";
        }
        av_frame_unref(this->hw_frame_);
        this->last_frame_time_ = time_sec;
    }
    
    // 4. Draw the Texture
    if (this->current_texture_id_ > 0 && this->current_egl_image_ != EGL_NO_IMAGE_KHR) {
        glUseProgram(this->external_program_);
        
        // Map UI coords [0..1] x [0..1] to projection coordinates
        float nx = x * 2.0f - 1.0f;
        float ny = 1.0f - y * 2.0f; // Invert Y
        float nw = w * 2.0f;
        float nh = h * 2.0f;
        
        float vertices[] = {
            nx,      ny - nh, src_x,         src_y + src_h,
            nx + nw, ny - nh, src_x + src_w, src_y + src_h,
            nx,      ny,      src_x,         src_y,
            nx + nw, ny,      src_x + src_w, src_y,
        };
        
        // Critical: Unbind the VBO so OpenGL reads from our local `vertices` pointer
        // instead of treating `&vertices[0]` as a massive byte offset into the VBO.
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        
        glVertexAttribPointer(this->external_pos_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), &vertices[0]);
        glEnableVertexAttribArray(this->external_pos_loc_);
        
        glVertexAttribPointer(this->external_tex_coord_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), &vertices[2]);
        glEnableVertexAttribArray(this->external_tex_coord_loc_);
        
        // Isolate video texture to GL_TEXTURE1 to prevent overwriting the GL_TEXTURE0 state used by UI
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, this->current_texture_id_);
        glUniform1i(this->external_sampler_loc_, 1);
        
        
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        
        glDisableVertexAttribArray(this->external_pos_loc_);
        glDisableVertexAttribArray(this->external_tex_coord_loc_);
        
        // **CRITICAL STATE CLEANUP**: Unbind the external texture and restore texture unit 0
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0); 
        
        // Restore the global Renderer VBO. Text and sprites assume `vbo_` is always bound.
        glBindBuffer(GL_ARRAY_BUFFER, renderer.vbo());
    }
    
    return !eof_reached;
}

} // namespace nuc_display::modules
