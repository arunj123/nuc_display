#include "modules/container_reader.hpp"
#include <iostream>

namespace nuc_display::modules {

ContainerReader::ContainerReader() {
    this->packet_ = av_packet_alloc();
}
    
ContainerReader::~ContainerReader() {
    if (this->packet_) {
        av_packet_free(&this->packet_);
    }
    if (this->format_ctx_) {
        avformat_close_input(&this->format_ctx_);
    }
}

std::expected<void, MediaError> ContainerReader::open(const std::string& filepath) {
    std::cout << "ContainerReader: Opening " << filepath << " using FFmpeg (Architecture Ready)\n";
    
    // Close any previously opened container to prevent double-open segfault
    if (this->format_ctx_) {
        avformat_close_input(&this->format_ctx_);
        this->format_ctx_ = nullptr;
    }
    
    if (avformat_open_input(&this->format_ctx_, filepath.c_str(), nullptr, nullptr) != 0) {
        return std::unexpected(MediaError::FileNotFound);
    }
    
    if (avformat_find_stream_info(this->format_ctx_, nullptr) < 0) {
        return std::unexpected(MediaError::DecodeFailed);
    }

    std::cout << "ContainerReader: Found " << this->format_ctx_->nb_streams << " streams.\n";
    return {};
}

int ContainerReader::find_video_stream() const {
    if (!this->format_ctx_) return -1;
    for (unsigned int i = 0; i < this->format_ctx_->nb_streams; i++) {
        if (this->format_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int ContainerReader::find_audio_stream() const {
    if (!this->format_ctx_) return -1;
    for (unsigned int i = 0; i < this->format_ctx_->nb_streams; i++) {
        if (this->format_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

AVCodecParameters* ContainerReader::get_codec_params(int stream_index) const {
    if (!this->format_ctx_ || stream_index < 0 || stream_index >= static_cast<int>(this->format_ctx_->nb_streams)) {
        return nullptr;
    }
    return this->format_ctx_->streams[stream_index]->codecpar;
}

AVRational ContainerReader::get_stream_timebase(int stream_index) const {
    if (!this->format_ctx_ || stream_index < 0 || stream_index >= static_cast<int>(this->format_ctx_->nb_streams)) {
        return AVRational{0, 1};
    }
    return this->format_ctx_->streams[stream_index]->time_base;
}

std::expected<AVPacket*, MediaError> ContainerReader::read_packet() {
    if (!this->format_ctx_ || !this->packet_) return std::unexpected(MediaError::InternalError);
    av_packet_unref(this->packet_);
    if (av_read_frame(this->format_ctx_, this->packet_) < 0) {
        return std::unexpected(MediaError::InternalError);
    }
    return this->packet_;
}

void ContainerReader::rewind() {
    if (this->format_ctx_) {
        av_seek_frame(this->format_ctx_, -1, 0, AVSEEK_FLAG_BACKWARD);
    }
}

} // namespace nuc_display::modules
