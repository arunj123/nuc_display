#include "modules/video_decoder.hpp"
#include <iostream>
#include <drm_fourcc.h>

namespace nuc_display::modules {

VideoDecoder::VideoDecoder() {
    this->hw_frame_ = av_frame_alloc();
    this->audio_frame_ = av_frame_alloc();
}

VideoDecoder::~VideoDecoder() {
    this->cleanup_codec();
    
    if (this->hw_frame_) {
        av_frame_free(&this->hw_frame_);
    }
    if (this->audio_frame_) {
        av_frame_free(&this->audio_frame_);
    }
    if (this->drm_frame_) {
        av_frame_free(&this->drm_frame_);
    }
    if (this->hw_device_ctx_) {
        av_buffer_unref(&this->hw_device_ctx_);
    }
}

std::expected<void, MediaError> VideoDecoder::init_v4l2(int drm_fd) {
    (void)drm_fd;
    std::cout << "[VideoDecoder] Initializing V4L2 M2M Hardware Decode Context (Raspberry Pi)\n";
    
    // Use DRM PRIME device type for zero-copy DMA-BUF frame export.
    // FFmpeg's h264_v4l2m2m decoder will use the V4L2 M2M kernel driver (bcm2835-codec)
    // and output frames as DRM PRIME (DMA-BUF) directly.
    int err = av_hwdevice_ctx_create(&this->hw_device_ctx_, AV_HWDEVICE_TYPE_DRM, "/dev/dri/card0", nullptr, 0);
    if (err < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(err, errbuf, sizeof(errbuf));
        std::cerr << "[VideoDecoder] Failed to create DRM HW device context: " << errbuf << "\n";
        std::cerr << "[VideoDecoder] V4L2 M2M decode will still work but zero-copy may not be available.\n";
        // Non-fatal — V4L2 decoder can still output DRM PRIME frames without this
    } else {
        std::cout << "[VideoDecoder] DRM HW device context initialized successfully.\n";
    }
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
    std::lock_guard<std::mutex> lock(this->queue_mutex_);
    
    while (!this->packet_queue_.empty()) {
        AVPacket* pkt = this->packet_queue_.front();
        av_packet_free(&pkt);
        this->packet_queue_.pop_front();
    }
    while (!this->video_frame_queue_.empty()) {
        AVFrame* frame = this->video_frame_queue_.front();
        av_frame_free(&frame);
        this->video_frame_queue_.pop_front();
    }
    while (!this->audio_frame_queue_.empty()) {
        AVFrame* frame = this->audio_frame_queue_.front();
        av_frame_free(&frame);
        this->audio_frame_queue_.pop_front();
    }

    if (this->codec_ctx_) {
        avcodec_free_context(&this->codec_ctx_);
        this->codec_ctx_ = nullptr;
    }
    if (this->audio_codec_ctx_) {
        avcodec_free_context(&this->audio_codec_ctx_);
        this->audio_codec_ctx_ = nullptr;
    }
    if (this->swr_ctx_) {
        swr_free(&this->swr_ctx_);
        this->swr_ctx_ = nullptr;
    }
    this->codec_ = nullptr;
    this->video_stream_index_ = -1;
    this->audio_stream_index_ = -1;
    this->last_frame_time_ = -1.0;
    this->video_start_time_ = -1.0;
    this->frames_rendered_ = 0;
    this->get_buffer_retry_count_ = 0;
    this->decoding_failure_count_ = 0;
    this->packets_sent_without_frame_ = 0;
    this->eof_reached_ = false;
    this->audio_spillover_.clear();
    this->audio_prebuffering_ = true;
    this->is_seeking_ = false;
    this->current_pos_sec_ = 0.0;
    this->seek_offset_sec_ = 0.0;
    this->alsa_error_count_ = 0;

    if (this->pcm_handle_) {
        snd_pcm_drop(this->pcm_handle_);
        snd_pcm_hw_free(this->pcm_handle_);
    }

    // Cleanup EGL resources
    if (this->current_egl_image_ != EGL_NO_IMAGE_KHR && this->egl_display_ != EGL_NO_DISPLAY) {
        PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR_ptr = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
        if (eglDestroyImageKHR_ptr) {
            eglDestroyImageKHR_ptr(this->egl_display_, this->current_egl_image_);
        }
        this->current_egl_image_ = EGL_NO_IMAGE_KHR;
    }
    if (this->current_texture_id_ != 0) {
        glDeleteTextures(1, &this->current_texture_id_);
        this->current_texture_id_ = 0;
    }
    if (this->external_program_ != 0) {
        glDeleteProgram(this->external_program_);
        this->external_program_ = 0;
    }
}

void VideoDecoder::load_playlist(const std::vector<std::string>& files) {
    if (files.empty()) return;
    this->playlist_ = files;
    this->playlist_index_ = 0;
    this->load(this->playlist_[this->playlist_index_]);
}

void VideoDecoder::next_video() {
    if (this->playlist_.empty()) return;
    
    int next_index = -1;
    {
        std::lock_guard<std::mutex> lock(this->queue_mutex_);
        this->playlist_index_ = (this->playlist_index_ + 1) % this->playlist_.size();
        next_index = this->playlist_index_;
    }
    
    if (next_index >= 0) {
        this->load(this->playlist_[next_index]);
    }
}

void VideoDecoder::unload() {
    std::cout << "[VideoDecoder] Unloading all resources and clearing playlist.\n";
    {
        std::lock_guard<std::mutex> lock(this->queue_mutex_);
        this->playlist_.clear();
        this->playlist_index_ = 0;
    }
    this->cleanup_codec();
}

bool VideoDecoder::is_loaded() const {
    return this->codec_ctx_ != nullptr;
}

void VideoDecoder::prev_video() {
    if (this->playlist_.empty()) return;
    
    int prev_index = -1;
    {
        std::lock_guard<std::mutex> lock(this->queue_mutex_);
        if (this->playlist_index_ == 0) {
            this->playlist_index_ = this->playlist_.size() - 1;
        } else {
            this->playlist_index_--;
        }
        prev_index = this->playlist_index_;
    }
    
    if (prev_index >= 0) {
        this->load(this->playlist_[prev_index]);
    }
}

void VideoDecoder::skip_forward(double seconds) {
    if (!this->codec_ctx_ || !this->container_.format_ctx() || seconds <= 0) return;
    
    double target_sec = this->current_pos_sec_ + seconds;
    double duration = this->container_.format_ctx()->duration / (double)AV_TIME_BASE;
    if (duration > 0 && target_sec > duration) {
        std::cout << "[VideoDecoder] Target " << target_sec << "s > Duration " << duration << "s. Clipping.\n";
        target_sec = duration - 0.5;
    }
    
    int64_t seek_target = this->container_.format_ctx()->start_time + static_cast<int64_t>(target_sec * AV_TIME_BASE);
    
    std::cout << "[VideoDecoder] Skipping forward " << seconds << "s (from " << this->current_pos_sec_ << "s to " << target_sec << "s)\n";
    {
        std::lock_guard<std::mutex> lock(this->queue_mutex_);
        while (!this->packet_queue_.empty()) { av_packet_free(&this->packet_queue_.front()); this->packet_queue_.pop_front(); }
        while (!this->video_frame_queue_.empty()) { av_frame_free(&this->video_frame_queue_.front()); this->video_frame_queue_.pop_front(); }
        while (!this->audio_frame_queue_.empty()) { av_frame_free(&this->audio_frame_queue_.front()); this->audio_frame_queue_.pop_front(); }
        
        this->eof_reached_ = false;
        this->audio_spillover_.clear();
        this->audio_prebuffering_ = true;
        this->video_start_time_ = -1.0;
        this->last_frame_time_ = -1.0;
        this->frames_rendered_ = 0;
        this->seek_offset_sec_ = target_sec;
        this->current_pos_sec_ = target_sec; 
        this->is_seeking_ = true;
    }
    
    av_seek_frame(this->container_.format_ctx(), -1, seek_target, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(this->codec_ctx_);
    if (this->audio_codec_ctx_) {
        avcodec_flush_buffers(this->audio_codec_ctx_);
        if (this->pcm_handle_) {
            snd_pcm_drop(this->pcm_handle_);
            snd_pcm_prepare(this->pcm_handle_);
        }
    }
}

void VideoDecoder::skip_backward(double seconds) {
    if (!this->codec_ctx_ || !this->container_.format_ctx()) return;
    
    double target_sec = std::max(0.0, this->current_pos_sec_ - seconds);
    int64_t seek_target = this->container_.format_ctx()->start_time + static_cast<int64_t>(target_sec * AV_TIME_BASE);

    std::cout << "[VideoDecoder] Skipping backward " << seconds << "s (from " << this->current_pos_sec_ << "s to " << target_sec << "s)\n";
    {
        std::lock_guard<std::mutex> lock(this->queue_mutex_);
        while (!this->packet_queue_.empty()) { av_packet_free(&this->packet_queue_.front()); this->packet_queue_.pop_front(); }
        while (!this->video_frame_queue_.empty()) { av_frame_free(&this->video_frame_queue_.front()); this->video_frame_queue_.pop_front(); }
        while (!this->audio_frame_queue_.empty()) { av_frame_free(&this->audio_frame_queue_.front()); this->audio_frame_queue_.pop_front(); }

        this->eof_reached_ = false;
        this->audio_spillover_.clear();
        this->audio_prebuffering_ = true;
        this->video_start_time_ = -1.0;
        this->last_frame_time_ = -1.0;
        this->frames_rendered_ = 0;
        this->seek_offset_sec_ = target_sec;
        this->current_pos_sec_ = target_sec;
        this->is_seeking_ = true;
    }

    av_seek_frame(this->container_.format_ctx(), -1, seek_target, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(this->codec_ctx_);
    if (this->audio_codec_ctx_) {
        avcodec_flush_buffers(this->audio_codec_ctx_);
        if (this->pcm_handle_) {
            snd_pcm_drop(this->pcm_handle_);
            snd_pcm_prepare(this->pcm_handle_);
        }
    }
}

std::expected<void, MediaError> VideoDecoder::load(const std::string& filepath) {
    std::cout << "[VideoDecoder] Loading " << filepath << std::endl;
    this->cleanup_codec();
    
    auto open_res = this->container_.open(filepath);
    if (!open_res) return open_res;

    this->video_stream_index_ = this->container_.find_video_stream();
    if (this->video_stream_index_ < 0) {
        return std::unexpected(MediaError::UnsupportedFormat);
    }

    AVCodecParameters* codec_params = this->container_.get_codec_params(this->video_stream_index_);
    this->stream_timebase_ = this->container_.get_stream_timebase(this->video_stream_index_);
    
    // PLATFORM_RPI: Only H.264 is supported by the VideoCore IV hardware decoder.
    // Reject all other codecs immediately.
    if (codec_params->codec_id != AV_CODEC_ID_H264) {
        const char* codec_name = avcodec_get_name(codec_params->codec_id);
        std::cerr << "[VideoDecoder] ERROR: Unsupported codec '" << codec_name 
                  << "' — Raspberry Pi hardware only supports H.264. Skipping file: " << filepath << "\n";
        return std::unexpected(MediaError::UnsupportedFormat);
    }

    // Try to find the V4L2 M2M hardware decoder first
    this->codec_ = const_cast<AVCodec*>(avcodec_find_decoder_by_name("h264_v4l2m2m"));
    if (!this->codec_) {
        std::cerr << "[VideoDecoder] h264_v4l2m2m decoder not found in FFmpeg. Trying generic h264.\n";
        this->codec_ = const_cast<AVCodec*>(avcodec_find_decoder(codec_params->codec_id));
    }
    
    if (this->codec_) {
        this->codec_ctx_ = avcodec_alloc_context3(this->codec_);
        avcodec_parameters_to_context(this->codec_ctx_, codec_params);
        
        // For V4L2 M2M, the decoder handles HW context internally.
        // The DRM device context helps with DMA-BUF export.
        if (this->hw_device_ctx_) {
            this->codec_ctx_->hw_device_ctx = av_buffer_ref(this->hw_device_ctx_);
        }
        
        // V4L2 M2M needs fewer extra HW frames than VA-API
        this->codec_ctx_->extra_hw_frames = 8;
        
        if (avcodec_open2(this->codec_ctx_, this->codec_, nullptr) < 0) {
            std::cerr << "[VideoDecoder] Failed to open H.264 V4L2 M2M decoder.\n";
            return std::unexpected(MediaError::DecodeFailed);
        }
        std::cout << "[VideoDecoder] Opened codec: " << this->codec_->name << "\n";
    } else {
        return std::unexpected(MediaError::UnsupportedFormat);
    }
    
    // Setup audio decoder if enabled (identical to NUC backend)
    if (this->audio_enabled_) {
        this->audio_stream_index_ = this->container_.find_audio_stream();
        if (this->audio_stream_index_ >= 0) {
            AVCodecParameters* a_params = this->container_.get_codec_params(this->audio_stream_index_);
            std::cout << "[VideoDecoder] Found audio stream at index " << this->audio_stream_index_ 
                      << " (Codec: " << a_params->codec_id << ")\n";
            const AVCodec* a_codec = avcodec_find_decoder(a_params->codec_id);
            if (a_codec) {
                this->audio_codec_ctx_ = avcodec_alloc_context3(a_codec);
                avcodec_parameters_to_context(this->audio_codec_ctx_, a_params);
                if (avcodec_open2(this->audio_codec_ctx_, a_codec, nullptr) == 0 && this->pcm_handle_) {
                    this->audio_spillover_.clear();
                    int channels = 2; 
                    unsigned int rate = 48000;
                    std::cout << "[VideoDecoder] Initializing ALSA PCM for " << rate << "Hz, " << channels << " channels\n";
                    
                    snd_pcm_hw_params_t *params;
                    snd_pcm_hw_params_alloca(&params);
                    snd_pcm_hw_params_any(this->pcm_handle_, params);
                    snd_pcm_hw_params_set_access(this->pcm_handle_, params, SND_PCM_ACCESS_RW_INTERLEAVED);
                    int dir = 0;
                    snd_pcm_hw_params_set_format(this->pcm_handle_, params, SND_PCM_FORMAT_S16_LE);
                    snd_pcm_hw_params_set_channels(this->pcm_handle_, params, channels);
                    snd_pcm_hw_params_set_rate_near(this->pcm_handle_, params, &rate, &dir);
                    
                    // Smaller buffer for Pi (0.5s instead of 1.5s)
                    snd_pcm_uframes_t buffer_size = rate / 2; // 0.5 seconds
                    snd_pcm_hw_params_set_buffer_size_near(this->pcm_handle_, params, &buffer_size);
                    snd_pcm_uframes_t period_size = rate / 10;
                    snd_pcm_hw_params_set_period_size_near(this->pcm_handle_, params, &period_size, &dir);
                    
                    int hw_err = snd_pcm_hw_params(this->pcm_handle_, params);
                    if (hw_err < 0) {
                        std::cerr << "ALSA: FATAL: Failed to apply hardware parameters: " << snd_strerror(hw_err) << "\n";
                        snd_pcm_close(this->pcm_handle_);
                        this->pcm_handle_ = nullptr;
                    } else {
                        snd_pcm_sw_params_t *sw_params;
                        snd_pcm_sw_params_alloca(&sw_params);
                        snd_pcm_sw_params_current(this->pcm_handle_, sw_params);
                        snd_pcm_sw_params_set_start_threshold(this->pcm_handle_, sw_params, 1);
                        snd_pcm_sw_params_set_avail_min(this->pcm_handle_, sw_params, period_size);
                        snd_pcm_sw_params(this->pcm_handle_, sw_params);
    
                        AVChannelLayout out_layout;
                        av_channel_layout_default(&out_layout, 2);
                        int swr_ret = swr_alloc_set_opts2(&this->swr_ctx_,
                            &out_layout, AV_SAMPLE_FMT_S16, (int)rate,
                            &this->audio_codec_ctx_->ch_layout, this->audio_codec_ctx_->sample_fmt, this->audio_codec_ctx_->sample_rate,
                            0, nullptr);
                        av_channel_layout_uninit(&out_layout);
                        
                        if (swr_ret == 0) {
                            this->negotiated_rate_ = rate;
                            swr_init(this->swr_ctx_);
                            std::cout << "[VideoDecoder] SwrContext initialized for " << av_get_sample_fmt_name(this->audio_codec_ctx_->sample_fmt) 
                                      << " (" << this->audio_codec_ctx_->sample_rate << "Hz) -> S16 (" << this->negotiated_rate_ << "Hz)\n";
                        }
                    }
                }
            }
        }
    }
    
    std::cout << "[VideoDecoder] Codec opened." << std::endl;
    return {};
}

void VideoDecoder::set_audio_enabled(bool enabled) {
    this->audio_enabled_ = enabled;
}

void VideoDecoder::init_audio(const std::string& device_name) {
    this->current_audio_device_ = device_name;
    if (this->pcm_handle_) {
        snd_pcm_close(this->pcm_handle_);
        this->pcm_handle_ = nullptr;
    }

    std::cout << "[VideoDecoder] Initializing ALSA Audio Device (Non-blocking): " << device_name << "\n";
    int err = snd_pcm_open(&this->pcm_handle_, device_name.c_str(), SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
    if (err < 0) {
        std::cerr << "ALSA: Cannot open audio device " << device_name << ": " << snd_strerror(err) << "\n";
        
        // Pi-optimized fallbacks: HDMI is typically plughw:0,0 or plughw:1,0
        std::vector<std::string> fallbacks = {"plughw:0,0", "plughw:1,0", "default"};
        for (const auto& fb : fallbacks) {
            if (fb == device_name) continue;
            std::cout << "ALSA: Trying fallback device '" << fb << "'...\n";
            err = snd_pcm_open(&this->pcm_handle_, fb.c_str(), SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
            if (err >= 0) {
                std::cout << "ALSA: Successfully opened fallback device '" << fb << "'\n";
                return;
            }
            std::cerr << "ALSA: Fallback to '" << fb << "' failed: " << snd_strerror(err) << "\n";
        }
        this->pcm_handle_ = nullptr;
    }
}

void VideoDecoder::set_paused(bool paused, double time_sec) {
    if (this->is_paused_ == paused) return;

    if (paused) {
        std::cout << "[VideoDecoder] Pausing playback at " << time_sec << "s\n";
        this->is_paused_ = true;
        this->pause_start_time_ = time_sec;
        
        if (this->pcm_handle_) {
            int err = snd_pcm_pause(this->pcm_handle_, 1);
            if (err < 0) {
                std::cout << "[VideoDecoder] ALSA pause not supported (" << snd_strerror(err) 
                          << "). Dropping PCM buffer.\n";
                snd_pcm_drop(this->pcm_handle_);
            }
        }
    } else {
        std::cout << "[VideoDecoder] Resuming playback at " << time_sec << "s\n";
        if (this->pause_start_time_ > 0 && this->video_start_time_ > 0) {
            double pause_duration = time_sec - this->pause_start_time_;
            this->video_start_time_ += pause_duration;
            std::cout << "[VideoDecoder] Shifted video_start_time by " << pause_duration << "s\n";
        }
        this->is_paused_ = false;
        this->pause_start_time_ = -1.0;
        
        if (this->pcm_handle_) {
            int err = snd_pcm_pause(this->pcm_handle_, 0);
            if (err < 0) {
                snd_pcm_prepare(this->pcm_handle_);
                this->audio_prebuffering_ = true;
            }
        }
    }
}

std::expected<void, MediaError> VideoDecoder::process(double time_sec) {
    (void)time_sec;
    if (!this->codec_ctx_ || this->is_paused_) return {};

    // 1a. Fill Packet Queue from Container
    while (true) {
        {
            std::lock_guard<std::mutex> lock(this->queue_mutex_);
            if (this->packet_queue_.size() >= this->max_packets_ || this->eof_reached_) break;
        }
        
        auto packet_res = this->container_.read_packet();
        if (!packet_res) {
            std::lock_guard<std::mutex> lock(this->queue_mutex_);
            this->eof_reached_ = true;
            break;
        }
        AVPacket* packet = av_packet_clone(packet_res.value());
        {
            std::lock_guard<std::mutex> lock(this->queue_mutex_);
            this->packet_queue_.push_back(packet);
            this->is_seeking_ = false;
        }
    }
    
    // 1b. Drain decoded frames from decoder
    while (true) {
        bool space_available = false;
        {
            std::lock_guard<std::mutex> lock(this->queue_mutex_);
            space_available = (this->video_frame_queue_.size() < this->max_video_frames_);
        }
        if (!space_available) break;

        AVFrame* frame = av_frame_alloc();
        int receive_res = avcodec_receive_frame(this->codec_ctx_, frame);
        if (receive_res == 0) {
            this->packets_sent_without_frame_ = 0;
            this->get_buffer_retry_count_ = 0;
            this->decoding_failure_count_ = 0;
            std::lock_guard<std::mutex> lock(this->queue_mutex_);
            this->video_frame_queue_.push_back(frame);
        } else {
            av_frame_free(&frame);
            if (receive_res == AVERROR(EAGAIN) || receive_res == AVERROR_EOF) {
                break;
            }
            
            if (receive_res == AVERROR(ENOMEM) || receive_res == AVERROR(EINVAL)) {
                 this->get_buffer_retry_count_++;
                 this->decoding_failure_count_++;
                 if (this->get_buffer_retry_count_ > 10) {
                     char err_buf[AV_ERROR_MAX_STRING_SIZE];
                     av_strerror(receive_res, err_buf, sizeof(err_buf));
                     std::cerr << "[VideoDecoder] Persistent get_buffer failure (" << err_buf << "). Flushing.\n";
                     avcodec_flush_buffers(this->codec_ctx_);
                     
                     std::lock_guard<std::mutex> lock(this->queue_mutex_);
                     while (!this->video_frame_queue_.empty()) {
                         AVFrame* f = this->video_frame_queue_.front();
                         av_frame_free(&f);
                         this->video_frame_queue_.pop_front();
                     }
                     while (!this->audio_frame_queue_.empty()) {
                         AVFrame* f = this->audio_frame_queue_.front();
                         av_frame_free(&f);
                         this->audio_frame_queue_.pop_front();
                     }
                     this->get_buffer_retry_count_ = 0;
                 }
            } else {
                 char err_buf[AV_ERROR_MAX_STRING_SIZE];
                 av_strerror(receive_res, err_buf, sizeof(err_buf));
                 std::cerr << "[VideoDecoder] Decoding error: " << err_buf << "\n";
                 this->decoding_failure_count_++;
            }

            if (this->decoding_failure_count_ > 50) {
                 std::cerr << "[VideoDecoder] Critical decoding failure threshold. Skipping.\n";
                 this->next_video();
                 return {};
            }
            break;
        }
    }
    
    // 1c. Send Packets to Decoder
    while (true) {
        AVPacket* packet = nullptr;
        {
            std::lock_guard<std::mutex> lock(this->queue_mutex_);
            if (this->packet_queue_.empty()) break;
            if (this->video_frame_queue_.size() >= this->max_video_frames_ && 
                (!this->audio_enabled_ || this->audio_frame_queue_.size() >= this->max_audio_frames_)) {
                break;
            }

            packet = this->packet_queue_.front();
            this->packet_queue_.pop_front();
        }
        
        bool packet_consumed = true;
        
        if (packet->stream_index == this->video_stream_index_) {
            int send_res = avcodec_send_packet(this->codec_ctx_, packet);
            if (send_res == 0) {
                this->packets_sent_without_frame_++;
                
                if (this->packets_sent_without_frame_ > 50) {
                    std::cerr << "[VideoDecoder] Sent 50 consecutive packets without frame. Skipping.\n";
                    av_packet_free(&packet);
                    this->next_video();
                    return {};
                }
            } else if (send_res == AVERROR(EAGAIN)) {
                std::lock_guard<std::mutex> lock(this->queue_mutex_);
                this->packet_queue_.push_front(packet);
                packet_consumed = false;
                break;
            }
        } else if (this->audio_enabled_ && packet->stream_index == this->audio_stream_index_ && this->audio_codec_ctx_) {
            if (avcodec_send_packet(this->audio_codec_ctx_, packet) == 0) {
                while (true) {
                    AVFrame* frame = av_frame_alloc();
                    if (avcodec_receive_frame(this->audio_codec_ctx_, frame) == 0) {
                        static int a_frames = 0;
                        if (++a_frames % 300 == 0) {
                            std::cout << "[VideoDecoder] Decoded 300 audio frames. Spillover: " 
                                      << this->audio_spillover_.size() << " bytes\n";
                        }
                        std::lock_guard<std::mutex> lock(this->queue_mutex_);
                        this->audio_frame_queue_.push_back(frame);
                        if (this->audio_frame_queue_.size() >= this->max_audio_frames_) break;
                    } else {
                        av_frame_free(&frame);
                        break;
                    }
                }
            }
        }
        
        if (packet_consumed) {
            av_packet_free(&packet);
        }
    }
    
    // 1d. Convert Audio Frames to PCM and push to ALSA spillover
    while (true) {
        AVFrame* frame = nullptr;
        {
            std::lock_guard<std::mutex> lock(this->queue_mutex_);
            if (this->audio_frame_queue_.empty()) break;
            frame = this->audio_frame_queue_.front();
            this->audio_frame_queue_.pop_front();
        }
        
        if (this->pcm_handle_ && this->swr_ctx_) {
            uint8_t* out_data[1];
            int64_t delay = swr_get_delay(this->swr_ctx_, this->audio_codec_ctx_->sample_rate);
            int out_samples = av_rescale_rnd(delay + frame->nb_samples, this->negotiated_rate_, this->audio_codec_ctx_->sample_rate, AV_ROUND_UP);
            
            std::vector<uint8_t> output_buffer(out_samples * 4); // S16 Stereo
            out_data[0] = output_buffer.data();
            int converted = swr_convert(this->swr_ctx_, out_data, out_samples, (const uint8_t**)frame->data, frame->nb_samples);
            
            if (converted > 0) {
                this->audio_spillover_.insert(this->audio_spillover_.end(), output_buffer.data(), output_buffer.data() + (converted * 4));
            } else if (converted < 0) {
                char err_buf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(converted, err_buf, sizeof(err_buf));
                std::cerr << "[VideoDecoder] swr_convert error: " << err_buf << "\n";
            }
        }
        av_frame_free(&frame);
    }

    // 1e. Persistent ALSA retry
    if (this->audio_enabled_ && !this->pcm_handle_ && !this->current_audio_device_.empty()) {
        static auto last_retry = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_retry).count() >= 5) {
            std::cout << "ALSA: Retrying to open device '" << this->current_audio_device_ << "'\n";
            this->init_audio(this->current_audio_device_);
            last_retry = now;
        }
    }

    // 1f. Write spillover to ALSA
    while (true) {
        if (!this->pcm_handle_ || this->audio_spillover_.empty()) break;
        
        size_t frame_size = 4; // S16 Stereo
        // Limit max buffer size (1.0s for Pi, less than NUC's 2.5s)
        if (this->audio_spillover_.size() > 48000 * 1.0 * frame_size) {
                this->audio_spillover_.erase(this->audio_spillover_.begin(), this->audio_spillover_.end() - (size_t)(48000 * 1.0 * frame_size));
        }
        
        if (this->audio_spillover_.size() < frame_size) break;
        
        size_t prebuffer_threshold = 48000 * 0.2 * frame_size;
        if (this->audio_prebuffering_) {
            if (this->audio_spillover_.size() < prebuffer_threshold) {
                break;
            }
            this->audio_prebuffering_ = false;
            std::cout << "ALSA: Pre-buffering complete (" << this->audio_spillover_.size() << " bytes ready)\n";
        }
        
        size_t bytes_to_grab = std::min(this->audio_spillover_.size(), (size_t)(4800 * 2) * frame_size);
        std::vector<uint8_t> block(this->audio_spillover_.begin(), this->audio_spillover_.begin() + bytes_to_grab);
        
        snd_pcm_sframes_t frames_to_write = block.size() / 4;
        snd_pcm_sframes_t written = snd_pcm_writei(this->pcm_handle_, block.data(), frames_to_write);
        
        if (written > 0) {
            this->alsa_error_count_ = 0;
            size_t bytes_written = written * 4;
            this->audio_spillover_.erase(this->audio_spillover_.begin(), this->audio_spillover_.begin() + bytes_written);
            if (written < frames_to_write) break;
        } else if (written == -EAGAIN) {
            break;
        } else if (written == -EPIPE) {
            std::cerr << "ALSA: Underrun. Preparing and re-buffering...\n";
            snd_pcm_prepare(this->pcm_handle_);
            this->audio_prebuffering_ = true;
            break;
        } else if (written == -ESTRPIPE) {
            std::cerr << "ALSA: Suspended. Resuming...\n";
            while ((written = snd_pcm_resume(this->pcm_handle_)) == -EAGAIN) sleep(1);
            if (written < 0) {
                snd_pcm_prepare(this->pcm_handle_);
                this->audio_prebuffering_ = true;
            }
            break;
        } else {
            if (written < 0) {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - this->last_alsa_error_log_).count() >= 5) {
                    std::cerr << "ALSA: Write error: " << snd_strerror(written) << " (" << written << "). Count: " << alsa_error_count_ << "\n";
                    this->last_alsa_error_log_ = now;
                }
                
                int res = snd_pcm_recover(this->pcm_handle_, written, 0);
                if (res < 0) {
                    if (++this->alsa_error_count_ > 10) {
                        std::cerr << "ALSA: Persistent failure. Forcing device close/reopen.\n";
                        snd_pcm_close(this->pcm_handle_);
                        this->pcm_handle_ = nullptr;
                        this->alsa_error_count_ = 0;
                    }
                }
                break;
            }
        }
    }
    
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
        // The vc4 EGL driver also performs YUV-to-RGB conversion automatically
        // when importing NV12 DMA-BUFs via EGLImage + external OES texture.
        const char* fs = R"(
            #extension GL_OES_EGL_image_external : require
            precision mediump float;
            varying vec2 v_texCoord;
            uniform samplerExternalOES s_texture;
            void main() {
                gl_FragColor = texture2D(s_texture, v_texCoord);
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
    
    // 2. Frame pacing
    AVFrame* frame_to_render = nullptr;
    {
        std::lock_guard<std::mutex> lock(this->queue_mutex_);
        if (!this->codec_ctx_ || this->is_paused_) return true;
        if (this->video_frame_queue_.empty()) {
            if (!this->is_seeking_ && this->eof_reached_ && this->packet_queue_.empty()) return false;
            return true;
        }
        
        AVFrame* first = this->video_frame_queue_.front();
        double fps = av_q2d(this->codec_ctx_->framerate);
        if (fps <= 0.0) fps = 30.0;
        double frame_pts = this->frames_rendered_ * (1.0 / fps);
        
        if (this->video_start_time_ < 0) {
            this->video_start_time_ = time_sec - frame_pts;
        }
        
        if (this->last_frame_time_ < 0) {
            this->last_frame_time_ = time_sec;
        }
        
        if (time_sec >= this->video_start_time_ + frame_pts) {
            frame_to_render = first;
            this->video_frame_queue_.pop_front();
            this->last_frame_time_ = time_sec;
            this->frames_rendered_++;
            this->current_pos_sec_ = this->seek_offset_sec_ + frame_pts;
        }
    }
    
    // 3. Map frame to DMA-BUF and create EGLImage (Zero-Copy)
    if (frame_to_render) {
        av_frame_unref(this->hw_frame_);
        av_frame_move_ref(this->hw_frame_, frame_to_render);
        av_frame_free(&frame_to_render);

        if (!this->drm_frame_) this->drm_frame_ = av_frame_alloc();
        
        av_frame_unref(this->drm_frame_);
        this->drm_frame_->format = AV_PIX_FMT_DRM_PRIME;
        
        // V4L2 M2M frames may already be in DRM PRIME format.
        // Try av_hwframe_map first, and if the frame is already DRM PRIME, use it directly.
        int map_result = -1;
        AVDRMFrameDescriptor* desc = nullptr;
        
        if (this->hw_frame_->format == AV_PIX_FMT_DRM_PRIME) {
            // Frame is already a DRM PRIME descriptor — use directly
            desc = (AVDRMFrameDescriptor*)this->hw_frame_->data[0];
            map_result = 0;
        } else {
            // Frame needs mapping from HW surface to DRM PRIME
            map_result = av_hwframe_map(this->drm_frame_, this->hw_frame_, AV_HWFRAME_MAP_READ);
            if (map_result == 0) {
                desc = (AVDRMFrameDescriptor*)this->drm_frame_->data[0];
            }
        }
        
        if (map_result == 0 && desc) {
            PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR_ptr = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
            PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR_ptr = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
            PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES_ptr = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
            
            if (eglCreateImageKHR_ptr && glEGLImageTargetTexture2DOES_ptr && desc && (desc->nb_layers > 0)) {
                uint32_t fourcc = desc->layers[0].format;
                
                if (this->current_egl_image_ != EGL_NO_IMAGE_KHR) {
                    eglDestroyImageKHR_ptr(this->egl_display_, this->current_egl_image_);
                    this->current_egl_image_ = EGL_NO_IMAGE_KHR;
                }
                
                std::vector<EGLint> attribs;
                attribs.push_back(EGL_WIDTH); attribs.push_back(this->codec_ctx_->width);
                attribs.push_back(EGL_HEIGHT); attribs.push_back(this->codec_ctx_->height);
                
                uint32_t import_format = fourcc;
                if (desc->nb_layers > 1 || (desc->nb_layers == 1 && desc->layers[0].nb_planes > 1)) {
                    if (import_format == 0x20203852 || import_format == 0) {
                        import_format = DRM_FORMAT_NV12;
                    }
                }
                
                attribs.push_back(EGL_LINUX_DRM_FOURCC_EXT); attribs.push_back(import_format);
                
                if (desc->nb_layers >= 2) {
                    // Multi-layer layout (Y in Layer 0, UV in Layer 1)
                    attribs.push_back(EGL_DMA_BUF_PLANE0_FD_EXT); 
                    attribs.push_back(desc->objects[desc->layers[0].planes[0].object_index].fd);
                    attribs.push_back(EGL_DMA_BUF_PLANE0_OFFSET_EXT); 
                    attribs.push_back(desc->layers[0].planes[0].offset);
                    attribs.push_back(EGL_DMA_BUF_PLANE0_PITCH_EXT); 
                    attribs.push_back(desc->layers[0].planes[0].pitch);
                    
                    attribs.push_back(EGL_DMA_BUF_PLANE1_FD_EXT); 
                    attribs.push_back(desc->objects[desc->layers[1].planes[0].object_index].fd);
                    attribs.push_back(EGL_DMA_BUF_PLANE1_OFFSET_EXT); 
                    attribs.push_back(desc->layers[1].planes[0].offset);
                    attribs.push_back(EGL_DMA_BUF_PLANE1_PITCH_EXT); 
                    attribs.push_back(desc->layers[1].planes[0].pitch);
                } else {
                    // Single-layer layout
                    for (int p = 0; p < desc->layers[0].nb_planes && p < 4; ++p) {
                        EGLint fd_key = EGL_DMA_BUF_PLANE0_FD_EXT + (p * 3);
                        EGLint offset_key = EGL_DMA_BUF_PLANE0_OFFSET_EXT + (p * 3);
                        EGLint pitch_key = EGL_DMA_BUF_PLANE0_PITCH_EXT + (p * 3);
                        
                        attribs.push_back(fd_key); 
                        attribs.push_back(desc->objects[desc->layers[0].planes[p].object_index].fd);
                        attribs.push_back(offset_key); 
                        attribs.push_back(desc->layers[0].planes[p].offset);
                        attribs.push_back(pitch_key); 
                        attribs.push_back(desc->layers[0].planes[p].pitch);
                    }
                }
            
            attribs.push_back(EGL_NONE);
            
            this->current_egl_image_ = eglCreateImageKHR_ptr(
                this->egl_display_, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attribs.data()
            );
            
            if (this->current_egl_image_ != EGL_NO_IMAGE_KHR) {
                glBindTexture(GL_TEXTURE_EXTERNAL_OES, this->current_texture_id_);
                glEGLImageTargetTexture2DOES_ptr(GL_TEXTURE_EXTERNAL_OES, this->current_egl_image_);
            } else {
                std::cerr << "[VideoDecoder] Failed to create EGLImageKHR from DMA-BUF.\n";
            }
        } else {
            std::cerr << "[VideoDecoder] EGL extensions missing or bad desc.\n";
        }
        } else {
            std::cerr << "[VideoDecoder] Failed to map V4L2 frame to DRM PRIME.\n";
        }
    }
    
    // 4. Draw the Texture
    if (this->current_texture_id_ > 0 && this->current_egl_image_ != EGL_NO_IMAGE_KHR) {
        glUseProgram(this->external_program_);
        
        float nx = x * 2.0f - 1.0f;
        float ny = 1.0f - y * 2.0f;
        float nw = w * 2.0f;
        float nh = h * 2.0f;
        
        float vertices[] = {
            nx,      ny - nh, src_x,         src_y + src_h,
            nx + nw, ny - nh, src_x + src_w, src_y + src_h,
            nx,      ny,      src_x,         src_y,
            nx + nw, ny,      src_x + src_w, src_y,
        };
        
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        
        glVertexAttribPointer(this->external_pos_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), &vertices[0]);
        glEnableVertexAttribArray(this->external_pos_loc_);
        
        glVertexAttribPointer(this->external_tex_coord_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), &vertices[2]);
        glEnableVertexAttribArray(this->external_tex_coord_loc_);
        
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, this->current_texture_id_);
        glUniform1i(this->external_sampler_loc_, 1);
        
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        
        glDisableVertexAttribArray(this->external_pos_loc_);
        glDisableVertexAttribArray(this->external_tex_coord_loc_);
        
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0); 
        
        glBindBuffer(GL_ARRAY_BUFFER, renderer.vbo());
    }
    
    return true;
}

} // namespace nuc_display::modules
