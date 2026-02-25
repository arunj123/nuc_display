#include "modules/video_decoder.hpp"
#include <iostream>
#include <drm_fourcc.h>

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
    if (this->swr_ctx_) {
        swr_free(&this->swr_ctx_);
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

    // Properly drain and reset ALSA to prevent EIO errors on next video
    if (this->pcm_handle_) {
        // Drain current buffer
        snd_pcm_drain(this->pcm_handle_);
        // Reset to prepared state for next stream
        snd_pcm_prepare(this->pcm_handle_);
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
    
    // Create a temporary copy to safely advance the index without holding the queue_mutex 
    // during the entire next_video() and load() sequence.
    int next_index = -1;
    {
        std::lock_guard<std::mutex> lock(this->queue_mutex_);
        this->playlist_index_ = (this->playlist_index_ + 1) % this->playlist_.size();
        next_index = this->playlist_index_;
    }
    
    // Call load() outside the lock, as load() calls cleanup_codec() which acquires queue_mutex_
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
        // Clear queues
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
    
    // For forward seek, using no flags can sometimes be better if we want to land near target.
    // However, FFmpeg often needs BACKWARD to find a reliable start point.
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
        // Clear queues
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
    std::cout << "VideoDecoder: Loading " << filepath << std::endl;
    this->cleanup_codec();
    
    auto open_res = this->container_.open(filepath);
    if (!open_res) return open_res;

    this->video_stream_index_ = this->container_.find_video_stream();
    if (this->video_stream_index_ < 0) {
        return std::unexpected(MediaError::UnsupportedFormat);
    }

    AVCodecParameters* codec_params = this->container_.get_codec_params(this->video_stream_index_);
    this->stream_timebase_ = this->container_.get_stream_timebase(this->video_stream_index_);
    this->codec_ = const_cast<AVCodec*>(avcodec_find_decoder(codec_params->codec_id));
    
    if (this->codec_) {
        this->codec_ctx_ = avcodec_alloc_context3(this->codec_);
        avcodec_parameters_to_context(this->codec_ctx_, codec_params);
        
        // Setup hardware decoding if context is available
        if (this->hw_device_ctx_) {
            this->codec_ctx_->hw_device_ctx = av_buffer_ref(this->hw_device_ctx_);
            this->codec_ctx_->get_format = [](AVCodecContext* ctx, const enum AVPixelFormat* pix_fmts) -> enum AVPixelFormat {
                for (const enum AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
                    if (*p == AV_PIX_FMT_VAAPI) {
                        return *p;
                    }
                }
                std::cerr << "VideoDecoder: VAAPI pixel format not supported by decoder. Falling back to software.\n";
                // CRITICAL: If we fallback to software, we MUST clear the hw_device_ctx
                // Otherwise FFmpeg tries to allocate VA-API surfaces for a software decoder, causing get_buffer() failures.
                av_buffer_unref(&ctx->hw_device_ctx);
                return pix_fmts[0];
            };
            // Headroom for internal queueing + reference frames
            this->codec_ctx_->extra_hw_frames = 32;
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
            std::cout << "VideoDecoder: Found audio stream at index " << this->audio_stream_index_ 
                      << " (Codec: " << a_params->codec_id << ")\n";
            const AVCodec* a_codec = avcodec_find_decoder(a_params->codec_id);
            if (a_codec) {
                this->audio_codec_ctx_ = avcodec_alloc_context3(a_codec);
                avcodec_parameters_to_context(this->audio_codec_ctx_, a_params);
                if (avcodec_open2(this->audio_codec_ctx_, a_codec, nullptr) == 0 && this->pcm_handle_) {
                    this->audio_spillover_.clear();
                    // ... (rest of init)
                    // HDMI strictly prefers 48000Hz Stereo
                    int channels = 2; 
                    unsigned int rate = 48000;
                    std::cout << "VideoDecoder: Initializing ALSA PCM for " << rate << "Hz, " << channels << " channels (FORCED)\n";
                    
                    snd_pcm_hw_params_t *params;
                    snd_pcm_hw_params_alloca(&params);
                    snd_pcm_hw_params_any(this->pcm_handle_, params);
                    snd_pcm_hw_params_set_access(this->pcm_handle_, params, SND_PCM_ACCESS_RW_INTERLEAVED);
                    int dir = 0;
                    // We are converting everything to S16 interleaved via swr_convert
                    snd_pcm_hw_params_set_format(this->pcm_handle_, params, SND_PCM_FORMAT_S16_LE);
                    snd_pcm_hw_params_set_channels(this->pcm_handle_, params, channels);
                    snd_pcm_hw_params_set_rate_near(this->pcm_handle_, params, &rate, &dir);
                    
                    // Set buffer and period sizes for smoother playback at ~30fps
                    // Increase buffer size to 1.5s to be extremely resilient to video thread decode spikes
                    snd_pcm_uframes_t buffer_size = rate + (rate / 2); // 1.5 seconds (72000 samples at 48k)
                    snd_pcm_hw_params_set_buffer_size_near(this->pcm_handle_, params, &buffer_size);
                    // 0.1s period
                    snd_pcm_uframes_t period_size = rate / 10;
                    snd_pcm_hw_params_set_period_size_near(this->pcm_handle_, params, &period_size, &dir);
                    
                    snd_pcm_hw_params(this->pcm_handle_, params);

                    // Initialize SwrContext for conversion to S16 interleaved Stereo
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
                        std::cout << "VideoDecoder: SwrContext initialized for " << av_get_sample_fmt_name(this->audio_codec_ctx_->sample_fmt) 
                                  << " (" << this->audio_codec_ctx_->sample_rate << "Hz) -> S16 (" << this->negotiated_rate_ << "Hz)\n";
                    }
                }
            }
        }
    }
    
    std::cout << "VideoDecoder: Codec opened." << std::endl;
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

    std::cout << "Initializing ALSA VideoDecoder Audio Device (Non-blocking): " << device_name << "\n";
    // Using SND_PCM_NONBLOCK to ensure we don't freeze the main display loop if the audio buffer is full
    int err = snd_pcm_open(&this->pcm_handle_, device_name.c_str(), SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
    if (err < 0) {
        std::cerr << "ALSA: Cannot open audio device " << device_name << ": " << snd_strerror(err) << "\n";
        
        // Strategy: if 'default' fails due to dmix/slave issues, try hardware directly
        // Card 0 Device 3 is ARZOPA (common display)
        std::vector<std::string> fallbacks = {"plughw:0,3", "plughw:0,7", "plughw:0,8", "plughw:0,0", "default"};
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

std::expected<void, MediaError> VideoDecoder::process(double time_sec) {
    (void)time_sec;
    if (!this->codec_ctx_) return {};

    // 1. Buffer Management: Refill queues if they are running low
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
            this->is_seeking_ = false; // Successfully read a packet after seek
        }
    }
    
    // 1b. Decode Packets into Frame Queues
    // Unconditionally drain the decoder FIRST to free internal hardware buffers.
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
            this->packets_sent_without_frame_ = 0; // Reset on successful decode
            this->get_buffer_retry_count_ = 0; // Reset on success
            this->decoding_failure_count_ = 0;
            std::lock_guard<std::mutex> lock(this->queue_mutex_);
            this->video_frame_queue_.push_back(frame);
        } else {
            av_frame_free(&frame);
            if (receive_res == AVERROR(EAGAIN) || receive_res == AVERROR_EOF) {
                break; // Decoder is empty or EOF
            }
            
            if (receive_res == AVERROR(ENOMEM) || receive_res == AVERROR(EINVAL)) {
                 this->get_buffer_retry_count_++;
                 this->decoding_failure_count_++;
                 if (this->get_buffer_retry_count_ > 10) {
                     char err_buf[AV_ERROR_MAX_STRING_SIZE];
                     av_strerror(receive_res, err_buf, sizeof(err_buf));
                     std::cerr << "VideoDecoder: Persistent get_buffer failure (" << err_buf << "). Flushing codec and reclaiming surfaces.\n";
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
                 std::cerr << "VideoDecoder: Decoding error: " << err_buf << "\n";
                 this->decoding_failure_count_++;
            }

            if (this->decoding_failure_count_ > 50) {
                 std::cerr << "VideoDecoder: Critical decoding failure threshold reached. Skipping.\n";
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
                break; // Yield to render()
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
                    std::cerr << "VideoDecoder: Sent 50 consecutive packets without receiving a frame. Skipping.\n";
                    av_packet_free(&packet);
                    this->next_video();
                    return {};
                }
            } else if (send_res == AVERROR(EAGAIN)) {
                // Decoder internal queue is FULL. Push back to top of queue.
                std::lock_guard<std::mutex> lock(this->queue_mutex_);
                this->packet_queue_.push_front(packet);
                packet_consumed = false;
                break; // Yield to render()
            } else {
                // Error (EOF or invalid). Drop packet.
            }
        } else if (this->audio_enabled_ && packet->stream_index == this->audio_stream_index_ && this->audio_codec_ctx_) {
            if (avcodec_send_packet(this->audio_codec_ctx_, packet) == 0) {
                while (true) {
                    AVFrame* frame = av_frame_alloc();
                    if (avcodec_receive_frame(this->audio_codec_ctx_, frame) == 0) {
                        // Diagnostic: log every 100th audio frame
                        static int a_frames = 0;
                        if (++a_frames % 300 == 0) {
                            std::cout << "VideoDecoder: Decoded 300 audio frames. Current spillover: " 
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
    
    // 1c. Convert Audio Frames to PCM and push to ALSA spillover
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
                // Already holding queue_mutex_ via lock_guard at top of process()
                this->audio_spillover_.insert(this->audio_spillover_.end(), output_buffer.data(), output_buffer.data() + (converted * 4));
            } else if (converted < 0) {
                char err_buf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(converted, err_buf, sizeof(err_buf));
                std::cerr << "VideoDecoder: swr_convert error: " << err_buf << "\n";
            }
        }
        av_frame_free(&frame);
    }

    // 1d. Move as much as possible from audio spillover to ALSA hardware
    // Persistent ALSA recovery: if pcm_handle is null and audio is enabled, try to re-init
    if (this->audio_enabled_ && !this->pcm_handle_ && !this->current_audio_device_.empty()) {
        static auto last_retry = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_retry).count() >= 5) {
            std::cout << "ALSA: Retrying to open device '" << this->current_audio_device_ << "'\n";
            this->init_audio(this->current_audio_device_);
            last_retry = now;
        }
    }

    while (true) {
        if (!this->pcm_handle_ || this->audio_spillover_.empty()) break;
        
        size_t frame_size = 4; // S16 Stereo
        // Limit max buffer size (e.g. 2.5 seconds) to prevent infinite memory usage if ALSA hangs
        if (this->audio_spillover_.size() > 48000 * 2.5 * frame_size) {
                this->audio_spillover_.erase(this->audio_spillover_.begin(), this->audio_spillover_.end() - (size_t)(48000 * 2.5 * frame_size));
        }
        
        if (this->audio_spillover_.size() < frame_size) break;
        
        // Pre-buffering chunk: Wait until we have at least 0.2s of audio before sending to avoid immediate under-runs
        size_t prebuffer_threshold = 48000 * 0.2 * frame_size;
        if (this->audio_prebuffering_) {
            if (this->audio_spillover_.size() < prebuffer_threshold) {
                break; // Still prebuffering, don't write to ALSA yet
            }
            this->audio_prebuffering_ = false; // Threshold reached, start writing
            std::cout << "ALSA: Pre-buffering complete (" << this->audio_spillover_.size() << " bytes ready)\n";
        }
        
        size_t bytes_to_grab = std::min(this->audio_spillover_.size(), (size_t)(4800 * 2) * frame_size); // write up to 0.2s at a time
        std::vector<uint8_t> block(this->audio_spillover_.begin(), this->audio_spillover_.begin() + bytes_to_grab);
        
        snd_pcm_sframes_t frames_to_write = block.size() / 4;
        snd_pcm_sframes_t written = snd_pcm_writei(this->pcm_handle_, block.data(), frames_to_write);
        
        if (written > 0) {
            this->alsa_error_count_ = 0;
            static int total_written = 0;
            total_written += written;
            if (total_written > 48000 * 5) { // every 5 seconds of audio
                std::cout << "VideoDecoder: ALSA Playback Check: Written " << written << " frames. Total in current run: " << total_written << "\n";
                total_written = 0;
            }
            size_t bytes_written = written * 4;
            this->audio_spillover_.erase(this->audio_spillover_.begin(), this->audio_spillover_.begin() + bytes_written);
            if (written < frames_to_write) break;
        } else if (written == -EAGAIN) {
            break;
        } else if (written == -EPIPE) {
            std::cerr << "ALSA: Underrun (EPIPE). Preparing and entering pre-buffering state...\n";
            snd_pcm_prepare(this->pcm_handle_); // Recover stream state
            this->audio_prebuffering_ = true;   // Wait for buffer to fill again
            break;
        } else if (written == -ESTRPIPE) {
            std::cerr << "ALSA: Suspended (ESTRPIPE). Resuming...\n";
            while ((written = snd_pcm_resume(this->pcm_handle_)) == -EAGAIN) sleep(1);
            if (written < 0) {
                snd_pcm_prepare(this->pcm_handle_);
                this->audio_prebuffering_ = true;
            }
            break;
        } else {
            if (written < 0) {
                // Log and try recover
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - this->last_alsa_error_log_).count() >= 5) {
                    std::cerr << "ALSA: Write error: " << snd_strerror(written) << " (" << written << "). Recovering (Count: " << alsa_error_count_ << ")...\n";
                    this->last_alsa_error_log_ = now;
                }
                
                // Try ALSA's built-in recover first
                int res = snd_pcm_recover(this->pcm_handle_, written, 0);
                if (res < 0) {
                    // Persistent failure - close for re-init
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
        // Require the external OES extension
        // With proper NV12 DMA-BUF import (Y + UV planes), the Intel EGL driver
        // performs the YUV-to-RGB conversion automatically. Passthrough shader.
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
    
    // 2. Determine if it's time to show a new frame
    AVFrame* frame_to_render = nullptr;
    {
        std::lock_guard<std::mutex> lock(this->queue_mutex_);
        if (!this->codec_ctx_) return false; // Codec was cleaned up (e.g. by next_video on another thread)
        if (this->video_frame_queue_.empty()) {
            // Only signal "done" when EOF is reached AND all packets have been consumed AND all frames shown
            // AND we are not currently waiting for a seek to complete.
            if (!this->is_seeking_ && this->eof_reached_ && this->packet_queue_.empty()) return false;
            return true; // Still have packets to decode or waiting for more
        }
        
        // Peek at the first frame for timing
        AVFrame* first = this->video_frame_queue_.front();
        // Hardware decoding (VA-API) often drops or misreports PTS (e.g. 0.0). 
        // Synthesize perfect uniform pacing using the codec framerate.
        double fps = av_q2d(this->codec_ctx_->framerate);
        if (fps <= 0.0) fps = 30.0; // Fallback to 30 FPS if framerate is unknown or vfr
        double frame_pts = this->frames_rendered_ * (1.0 / fps);
        
        if (this->video_start_time_ < 0) {
            this->video_start_time_ = time_sec - frame_pts; // Anchor the video time
        }
        
        if (this->last_frame_time_ < 0) {
            this->last_frame_time_ = time_sec; // Initialize last_frame_time_
        }
        
        // Pacing logic: if current program time has passed frame PTS, show it
        if (time_sec >= this->video_start_time_ + frame_pts) {
            frame_to_render = first;
            this->video_frame_queue_.pop_front();
            this->last_frame_time_ = time_sec;
            this->frames_rendered_++;
            this->current_pos_sec_ = this->seek_offset_sec_ + frame_pts; // Absolute position
        }
    }
    
    // 3. If a new frame is ready, update the EGL texture. Otherwise, keep the old one.
    if (frame_to_render) {
        // Map Frame to DMA-BUF and Create EGLImage (Zero-Copy)
        av_frame_unref(this->hw_frame_);
        av_frame_move_ref(this->hw_frame_, frame_to_render);
        av_frame_free(&frame_to_render);

        if (!this->drm_frame_) this->drm_frame_ = av_frame_alloc();
        
        // Unref the PREVIOUS DRM mapping before creating a new one.
        // (The previous one was kept alive so the GPU could safely read from it.)
        av_frame_unref(this->drm_frame_);
        this->drm_frame_->format = AV_PIX_FMT_DRM_PRIME;
        if (av_hwframe_map(this->drm_frame_, this->hw_frame_, AV_HWFRAME_MAP_READ) == 0) {
            AVDRMFrameDescriptor* desc = (AVDRMFrameDescriptor*)this->drm_frame_->data[0];
            
            PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR_ptr = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
            PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR_ptr = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
            PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES_ptr = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
            
            if (eglCreateImageKHR_ptr && glEGLImageTargetTexture2DOES_ptr && desc && (desc->nb_layers > 0)) {
                // Log the fourcc format for debugging
                uint32_t fourcc = desc->layers[0].format;
                
                if (this->current_egl_image_ != EGL_NO_IMAGE_KHR) {
                    eglDestroyImageKHR_ptr(this->egl_display_, this->current_egl_image_);
                    this->current_egl_image_ = EGL_NO_IMAGE_KHR;
                }
                
                std::vector<EGLint> attribs;
                attribs.push_back(EGL_WIDTH); attribs.push_back(this->codec_ctx_->width);
                attribs.push_back(EGL_HEIGHT); attribs.push_back(this->codec_ctx_->height);
                
                // Intelligent format selection: If multiple planes/layers exist, it's likely NV12 
                // regardless of what FFmpeg's DRM_PRIME mapping claims for the first layer format.
                uint32_t import_format = fourcc;
                if (desc->nb_layers > 1 || (desc->nb_layers == 1 && desc->layers[0].nb_planes > 1)) {
                    if (import_format == 0x20203852 || import_format == 0) { // R8 or unknown
                        import_format = DRM_FORMAT_NV12;
                    }
                }
                
                attribs.push_back(EGL_LINUX_DRM_FOURCC_EXT); attribs.push_back(import_format);
                
                if (desc->nb_layers >= 2) {
                    // Multi-layer layout (e.g. Y in Layer 0, UV in Layer 1)
                    // Plane 0 (Y)
                    attribs.push_back(EGL_DMA_BUF_PLANE0_FD_EXT); 
                    attribs.push_back(desc->objects[desc->layers[0].planes[0].object_index].fd);
                    attribs.push_back(EGL_DMA_BUF_PLANE0_OFFSET_EXT); 
                    attribs.push_back(desc->layers[0].planes[0].offset);
                    attribs.push_back(EGL_DMA_BUF_PLANE0_PITCH_EXT); 
                    attribs.push_back(desc->layers[0].planes[0].pitch);
                    
                    // Plane 1 (UV)
                    attribs.push_back(EGL_DMA_BUF_PLANE1_FD_EXT); 
                    attribs.push_back(desc->objects[desc->layers[1].planes[0].object_index].fd);
                    attribs.push_back(EGL_DMA_BUF_PLANE1_OFFSET_EXT); 
                    attribs.push_back(desc->layers[1].planes[0].offset);
                    attribs.push_back(EGL_DMA_BUF_PLANE1_PITCH_EXT); 
                    attribs.push_back(desc->layers[1].planes[0].pitch);
                } else {
                    // Single-layer layout (planes in desc->layers[0])
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
                std::cerr << "VideoDecoder: Failed to create EGLImageKHR from DMA-BUF.\n";
            }
        } else {
            std::cerr << "VideoDecoder: EGL extensions missing or bad desc.\n";
        }
        // IMPORTANT: Do NOT unref drm_frame_ or hw_frame_ here!
        // The EGL image references the DMA-BUF FDs from drm_frame_, which in turn
        // reference the VA-API surface in hw_frame_. Unreffing them would return the
        // surface to the decoder pool, allowing the decoder to overwrite it while
        // the GPU is still reading from it — causing character-level flickering.
        // They will be unreffed at the TOP of this block when the NEXT frame arrives.
        } else {
            std::cerr << "VideoDecoder: Failed to map VAAPI frame to DRM PRIME.\n";
        }
    } // end if (frame_to_render)
    
    // 4. Draw the Texture (ALWAYS — even when reusing the previous frame's texture)
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
    
    // The true end-of-video condition (eof_reached_ AND empty queue) is handled
    // by the early return at the top of this function. Here we always return true
    // to keep playing — there may still be decoded frames waiting to be displayed
    // even after the container has been fully read.
    return true;
}

} // namespace nuc_display::modules
