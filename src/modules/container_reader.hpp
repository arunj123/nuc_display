#pragma once

#include <string>
#include <expected>
#include <vector>
#include <memory>
#include "modules/media_module.hpp"

extern "C" {
#include <libavformat/avformat.h>
}

namespace nuc_display::modules {

class ContainerReader {
public:
    ContainerReader();
    ~ContainerReader();

    std::expected<void, MediaError> open(const std::string& filepath);
    
    int find_video_stream() const;
    int find_audio_stream() const;
    AVCodecParameters* get_codec_params(int stream_index) const;
    std::expected<AVPacket*, MediaError> read_packet();

    void rewind();

private:
    AVFormatContext* format_ctx_ = nullptr;
    AVPacket* packet_ = nullptr;
};

} // namespace nuc_display::modules
