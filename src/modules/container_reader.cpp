#include "modules/container_reader.hpp"
#include <iostream>

namespace nuc_display::modules {

ContainerReader::ContainerReader() {}

ContainerReader::~ContainerReader() {
    if (this->format_ctx_) {
        avformat_close_input(&this->format_ctx_);
    }
}

std::expected<void, MediaError> ContainerReader::open(const std::string& filepath) {
    std::cout << "ContainerReader: Opening " << filepath << " using FFmpeg (Architecture Ready)\n";
    if (avformat_open_input(&this->format_ctx_, filepath.c_str(), nullptr, nullptr) != 0) {
        return std::unexpected(MediaError::FileNotFound);
    }
    
    if (avformat_find_stream_info(this->format_ctx_, nullptr) < 0) {
        return std::unexpected(MediaError::DecodeFailed);
    }

    std::cout << "ContainerReader: Found " << this->format_ctx_->nb_streams << " streams.\n";
    return {};
}

} // namespace nuc_display::modules
