#include "modules/video_decoder.hpp"
#include <iostream>

namespace nuc_display::modules {

VideoDecoder::VideoDecoder() {
    // Basic FFmpeg initialization (av_register_all is deprecated in modern ffmpeg)
}

VideoDecoder::~VideoDecoder() {
    if (this->codec_ctx_) {
        avcodec_free_context(&this->codec_ctx_);
    }
    if (this->hw_device_ctx_) {
        av_buffer_unref(&this->hw_device_ctx_);
    }
}

std::expected<void, MediaError> VideoDecoder::init_vaapi(int drm_fd) {
    // Scaffold for VA-API initialization using Intel hardware
    std::cout << "Initializing VA-API on DRM FD: " << drm_fd << "\n";
    
    int err = av_hwdevice_ctx_create(&this->hw_device_ctx_, AV_HWDEVICE_TYPE_VAAPI, nullptr, nullptr, 0);
    if (err < 0) {
        std::cerr << "Failed to create VA-API hardware device context\n";
        return std::unexpected(MediaError::HardwareError);
    }

    std::cout << "VA-API hardware acceleration initialized.\n";
    return {};
}

std::expected<void, MediaError> VideoDecoder::load(const std::string& filepath) {
    std::cout << "VideoDecoder: Loading " << filepath << " (Architecture Ready)\n";
    // Future implementation: Open file, find stream, initialize codec with VA_API
    return {};
}

std::expected<void, MediaError> VideoDecoder::process(double time_sec) {
    // Future implementation: Decode frame at time_sec using hardware surfaces
    (void)time_sec;
    return {};
}

} // namespace nuc_display::modules
