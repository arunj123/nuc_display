#include "modules/audio_player.hpp"
#include <iostream>

namespace nuc_display::modules {

AudioPlayer::AudioPlayer() {}

AudioPlayer::~AudioPlayer() {
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
    std::cout << "AudioPlayer: Loading " << filepath << " (Architecture Ready)\n";
    return {};
}

std::expected<void, MediaError> AudioPlayer::process(double time_sec) {
    (void)time_sec;
    return {};
}

} // namespace nuc_display::modules
