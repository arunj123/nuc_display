#include "modules/audio_player.hpp"
#include <iostream>

namespace nuc_display::modules {

AudioPlayer::AudioPlayer() {
    this->frame_ = av_frame_alloc();
}

AudioPlayer::~AudioPlayer() {
    if (this->frame_) {
        av_frame_free(&this->frame_);
    }
    if (this->codec_ctx_) {
        avcodec_free_context(&this->codec_ctx_);
    }
    if (this->pcm_handle_) {
        snd_pcm_close(this->pcm_handle_);
    }
}

std::expected<void, MediaError> AudioPlayer::init_alsa(const std::string& device_name) {
    std::cout << "Initializing ALSA device: " << device_name << "\n";
    int err = snd_pcm_open(&this->pcm_handle_, device_name.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        std::cerr << "ALSA: Cannot open audio device: " << snd_strerror(err) << "\n";
        return std::unexpected(MediaError::HardwareError);
    }
    std::cout << "ALSA initialized.\n";
    return {};
}

std::expected<void, MediaError> AudioPlayer::load(const std::string& filepath) {
    std::cout << "AudioPlayer: Loading " << filepath << "\n";
    auto open_res = this->container_.open(filepath);
    if (!open_res) return open_res;

    this->audio_stream_index_ = this->container_.find_audio_stream();
    if (this->audio_stream_index_ < 0) {
        return std::unexpected(MediaError::UnsupportedFormat);
    }

    AVCodecParameters* codec_params = this->container_.get_codec_params(this->audio_stream_index_);
    const AVCodec* decoder = avcodec_find_decoder(codec_params->codec_id);
    if (!decoder) {
        return std::unexpected(MediaError::UnsupportedFormat);
    }

    this->codec_ctx_ = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(this->codec_ctx_, codec_params);
    
    if (avcodec_open2(this->codec_ctx_, decoder, nullptr) < 0) {
        return std::unexpected(MediaError::DecodeFailed);
    }

    std::cout << "AudioPlayer: Codec " << decoder->name << " opened.\n";
    return {};
}

std::expected<void, MediaError> AudioPlayer::process(double time_sec) {
    (void)time_sec;
    auto packet_res = this->container_.read_packet();
    if (!packet_res) return std::unexpected(packet_res.error());

    AVPacket* packet = packet_res.value();
    if (packet->stream_index != this->audio_stream_index_) {
        return {};
    }

    if (avcodec_send_packet(this->codec_ctx_, packet) < 0) {
        return std::unexpected(MediaError::DecodeFailed);
    }

    while (avcodec_receive_frame(this->codec_ctx_, this->frame_) >= 0) {
        // Here we would normally write to ALSA
        // snd_pcm_writei(this->pcm_handle_, this->frame_->data[0], this->frame_->nb_samples);
        std::cout << "AudioPlayer: Decoded frame with " << this->frame_->nb_samples << " samples.\n";
        av_frame_unref(this->frame_);
    }

    return {};
}

} // namespace nuc_display::modules
