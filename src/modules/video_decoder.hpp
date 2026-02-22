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

#include "modules/container_reader.hpp"

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
    ContainerReader container_;
    int video_stream_index_ = -1;
    AVCodecContext* codec_ctx_ = nullptr;
    AVCodecParserContext* parser_ = nullptr;
    AVCodec* codec_ = nullptr;
    AVBufferRef* hw_device_ctx_ = nullptr;
    AVFrame* frame_ = nullptr;
    
    // VA-API State
    VADisplay va_display_ = nullptr;
    VAConfigID va_config_id_ = VA_INVALID_ID;
    VAContextID va_context_id_ = VA_INVALID_ID;
    std::vector<VASurfaceID> surfaces_;
};

} // namespace nuc_display::modules
