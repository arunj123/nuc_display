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
    
    // Future: Method to extract streams and feed them to VideoDecoder/AudioPlayer
    
private:
    AVFormatContext* format_ctx_ = nullptr;
};

} // namespace nuc_display::modules
