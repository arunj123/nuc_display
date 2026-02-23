#include "stubs_alsa.hpp"
#include <cstring>
#include <iostream>

namespace nuc_display::tests::mock {
    AlsaMockState g_alsa_mock;
} // namespace nuc_display::tests::mock

using namespace nuc_display::tests::mock;

extern "C" {

int snd_pcm_open(snd_pcm_t **pcm, const char *name, 
                 snd_pcm_stream_t stream, int mode) {
    if (g_alsa_mock.fail_open) return -1;
    *pcm = reinterpret_cast<snd_pcm_t*>(0xDEADBEEF);
    return 0;
}

int snd_pcm_set_params(snd_pcm_t *pcm,
                       snd_pcm_format_t format,
                       snd_pcm_access_t access,
                       unsigned int channels,
                       unsigned int rate,
                       int soft_resample,
                       unsigned int latency) {
    if (g_alsa_mock.fail_set_params) return -1;
    return 0;
}

int snd_pcm_close(snd_pcm_t *pcm) {
    return 0; 
}

int snd_pcm_prepare(snd_pcm_t *pcm) {
    return 0;
}

int snd_pcm_drop(snd_pcm_t *pcm) {
    return 0;
}

snd_pcm_sframes_t snd_pcm_writei(snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size) {
    if (g_alsa_mock.simulate_underrun) {
        g_alsa_mock.simulate_underrun = false; // Underrun once
        return -EPIPE; 
    }
    g_alsa_mock.written_frames.push_back((int)size);
    return size;
}

int snd_pcm_recover(snd_pcm_t *pcm, int err, int silent) {
    if (g_alsa_mock.recover_fail_count > 0) {
        g_alsa_mock.recover_fail_count--;
        return -1; // Fail recovery
    }
    return 0; // Success recovery
}

// Stubs for other used symbols
const char *snd_strerror(int errnum) {
    return "Mock ALSA Error";
}

} // extern C
