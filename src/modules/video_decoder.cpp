#include "modules/video_decoder.hpp"
#include <iostream>

namespace nuc_display::modules {

VideoDecoder::VideoDecoder() {
    this->frame_ = av_frame_alloc();
}

VideoDecoder::~VideoDecoder() {
    if (this->va_display_) {
        if (this->va_context_id_ != VA_INVALID_ID) vaDestroyContext(this->va_display_, this->va_context_id_);
        if (this->va_config_id_ != VA_INVALID_ID) vaDestroyConfig(this->va_display_, this->va_config_id_);
        if (!this->surfaces_.empty()) {
            vaDestroySurfaces(this->va_display_, this->surfaces_.data(), this->surfaces_.size());
        }
        // vaTerminate(this->va_display_); // Don't terminate if display is shared
    }
    if (this->parser_) {
        av_parser_close(this->parser_);
    }
    if (this->frame_) {
        av_frame_free(&this->frame_);
    }
    if (this->codec_ctx_) {
        avcodec_free_context(&this->codec_ctx_);
    }
}

std::expected<void, MediaError> VideoDecoder::init_vaapi(int drm_fd) {
    std::cout << "Initializing Direct VA-API on DRM FD: " << drm_fd << "\n";
    
    this->va_display_ = vaGetDisplayDRM(drm_fd);
    if (!this->va_display_) {
        std::cerr << "Failed to get VA display from DRM FD\n";
        return std::unexpected(MediaError::HardwareError);
    }

    int major, minor;
    VAStatus status = vaInitialize(this->va_display_, &major, &minor);
    if (status != VA_STATUS_SUCCESS) {
        std::cerr << "Failed to initialize VA-API: " << vaErrorStr(status) << "\n";
        return std::unexpected(MediaError::HardwareError);
    }

    std::cout << "VA-API initialized: version " << major << "." << minor << "\n";
    return {};
}

std::expected<void, MediaError> VideoDecoder::load(const std::string& filepath) {
    std::cout << "VideoDecoder: Loading " << filepath << "\n";
    
    auto open_res = this->container_.open(filepath);
    if (!open_res) return open_res;

    this->video_stream_index_ = this->container_.find_video_stream();
    if (this->video_stream_index_ < 0) {
        return std::unexpected(MediaError::UnsupportedFormat);
    }

    AVCodecParameters* codec_params = this->container_.get_codec_params(this->video_stream_index_);
    this->codec_ = const_cast<AVCodec*>(avcodec_find_decoder(codec_params->codec_id));
    
    if (this->codec_) {
        this->codec_ctx_ = avcodec_alloc_context3(this->codec_);
        avcodec_parameters_to_context(this->codec_ctx_, codec_params);
        // We don't open the codec for decoding, just use it for the parser
    }

    // Initialize Parser (for extracting NAL bits later)
    this->parser_ = av_parser_init(codec_params->codec_id);
    if (!this->parser_) {
        return std::unexpected(MediaError::InternalError);
    }

    // VA-API Config Creation (Bare-bone)
    VAConfigAttrib attrib;
    attrib.type = VAConfigAttribRTFormat;
    VAStatus status = vaGetConfigAttributes(this->va_display_, VAProfileH264Main, VAEntrypointVLD, &attrib, 1);
    
    status = vaCreateConfig(this->va_display_, VAProfileH264Main, VAEntrypointVLD, &attrib, 1, &this->va_config_id_);
    if (status != VA_STATUS_SUCCESS) {
        return std::unexpected(MediaError::HardwareError);
    }

    // Create Surfaces
    this->surfaces_.resize(4); // Pool of 4 surfaces
    status = vaCreateSurfaces(
        this->va_display_,
        VA_RT_FORMAT_YUV420, codec_params->width, codec_params->height,
        this->surfaces_.data(), this->surfaces_.size(),
        nullptr, 0
    );

    // Create Context
    status = vaCreateContext(
        this->va_display_, this->va_config_id_,
        codec_params->width, codec_params->height,
        VA_PROGRESSIVE,
        this->surfaces_.data(), this->surfaces_.size(),
        &this->va_context_id_
    );

    if (status != VA_STATUS_SUCCESS) {
        return std::unexpected(MediaError::HardwareError);
    }

    std::cout << "VideoDecoder: VA-API Context created (" << codec_params->width << "x" << codec_params->height << ")\n";
    return {};
}

std::expected<void, MediaError> VideoDecoder::process(double time_sec) {
    (void)time_sec;
    
    auto packet_res = this->container_.read_packet();
    if (!packet_res) return std::unexpected(packet_res.error());

    AVPacket* packet = packet_res.value();
    if (packet->stream_index != this->video_stream_index_) {
        return {};
    }

    uint8_t* data = packet->data;
    int size = packet->size;

    while (size > 0) {
        uint8_t* out_data = nullptr;
        int out_size = 0;
        int len = av_parser_parse2(this->parser_, this->codec_ctx_, &out_data, &out_size, data, size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        
        if (len < 0) break;
        data += len;
        size -= len;

        if (out_size > 0) {
            // Found a NAL unit (frame or slice)
            // Bare-bone: We should parse this for SPS/PPS/Slice header
            // For now, we signal VA-API that we are starting a frame
            VASurfaceID target_surface = this->surfaces_[0]; // Simplified
            
            vaBeginPicture(this->va_display_, this->va_context_id_, target_surface);
            
            // In a real bare-metal implementation, we would create 
            // VAPictureParameterBuffer, VAIQMatrixBuffer, and VASliceDataBuffer here.
            // Since parsing H.264 headers is out of scope for a single task,
            // we will demonstrate the buffer submission logic.
            
            VABufferID bitstream_buf;
            vaCreateBuffer(this->va_display_, this->va_context_id_, VASliceDataBufferType, out_size, 1, out_data, &bitstream_buf);
            vaRenderPicture(this->va_display_, this->va_context_id_, &bitstream_buf, 1);
            
            vaEndPicture(this->va_display_, this->va_context_id_);
            vaSyncSurface(this->va_display_, target_surface);
            
            std::cout << "VideoDecoder: VA-API rendered NAL unit (size " << out_size << ")\n";
            vaDestroyBuffer(this->va_display_, bitstream_buf);
        }
    }

    return {};
}

} // namespace nuc_display::modules
