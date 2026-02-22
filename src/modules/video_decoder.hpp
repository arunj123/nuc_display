#pragma once

#include "modules/media_module.hpp"
#include <string>
#include <expected>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <va/va.h>
#include <va/va_drm.h>
}

namespace nuc_display::modules {

class VideoDecoder : public MediaModule {
public:
    VideoDecoder();
    ~VideoDecoder() override;

    std::expected<void, MediaError> load(const std::string& filepath) override;
    std::expected<void, MediaError> process(double time_sec) override;

    // VA-API specific initialization
    std::expected<void, MediaError> init_vaapi(int drm_fd);

private:
    AVCodecContext* codec_ctx_ = nullptr;
    AVBufferRef* hw_device_ctx_ = nullptr;
    
    // VA-API State
    VADisplay va_display_ = nullptr;
};

} // namespace nuc_display::modules
