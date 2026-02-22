#pragma once

#include "modules/media_module.hpp"
#include <string>
#include <expected>
#include <alsa/asoundlib.h>
extern "C" {
#include <libavcodec/avcodec.h>
}
#include "modules/container_reader.hpp"

namespace nuc_display::modules {

class AudioPlayer : public MediaModule {
public:
    AudioPlayer();
    ~AudioPlayer() override;

    std::expected<void, MediaError> load(const std::string& filepath) override;
    std::expected<void, MediaError> process(double time_sec) override;

    std::expected<void, MediaError> init_alsa(const std::string& device_name = "default");

private:
    ContainerReader container_;
    int audio_stream_index_ = -1;
    AVCodecContext* codec_ctx_ = nullptr;
    AVFrame* frame_ = nullptr;
    snd_pcm_t* pcm_handle_ = nullptr;
};

} // namespace nuc_display::modules
