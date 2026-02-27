#include "stubs_alsa.hpp"
#include <cstring>
#include <iostream>

namespace nuc_display::tests::mock {
    AlsaMockState g_alsa_mock;
    CurlMockState g_curl_mock;
} // namespace nuc_display::tests::mock

using namespace nuc_display::tests::mock;

extern "C" {

int snd_pcm_open(snd_pcm_t **pcm, const char *name, 
                 snd_pcm_stream_t stream, int mode) {
    (void)name; (void)stream; (void)mode;
    if (g_alsa_mock.fail_open) return -1;
    *pcm = reinterpret_cast<snd_pcm_t*>(0xDEADBEEF);
    g_alsa_mock.state = PcmState::OPEN;
    return 0;
}

int snd_pcm_hw_params_any(snd_pcm_t *pcm, snd_pcm_hw_params_t *params) {
    (void)pcm; (void)params;
    g_alsa_mock.hw_params_any_called = true;
    return 0;
}

int snd_pcm_hw_params_set_access(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, snd_pcm_access_t _access) { (void)pcm; (void)params; (void)_access; return 0; }
int snd_pcm_hw_params_set_format(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, snd_pcm_format_t val) { (void)pcm; (void)params; (void)val; return 0; }
int snd_pcm_hw_params_set_channels(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int val) { (void)pcm; (void)params; (void)val; return 0; }
int snd_pcm_hw_params_set_rate_near(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int *val, int *dir) { (void)pcm; (void)params; (void)val; (void)dir; return 0; }
int snd_pcm_hw_params_set_buffer_size_near(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, snd_pcm_uframes_t *val) { (void)pcm; (void)params; (void)val; return 0; }
int snd_pcm_hw_params_set_period_size_near(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, snd_pcm_uframes_t *val, int *dir) { (void)pcm; (void)params; (void)val; (void)dir; return 0; }

int snd_pcm_hw_params(snd_pcm_t *pcm, snd_pcm_hw_params_t *params) {
    (void)pcm; (void)params;
    if (g_alsa_mock.state != PcmState::OPEN && g_alsa_mock.state != PcmState::SETUP) {
        return -EBUSY; // Hardware parameters can only be changed in OPEN or SETUP state
    }
    g_alsa_mock.state = PcmState::PREPARED;
    return 0;
}

int snd_pcm_hw_free(snd_pcm_t *pcm) {
    (void)pcm;
    g_alsa_mock.state = PcmState::SETUP;
    return 0;
}

int snd_pcm_sw_params_current(snd_pcm_t *pcm, snd_pcm_sw_params_t *params) { (void)pcm; (void)params; return 0; }
int snd_pcm_sw_params_set_start_threshold(snd_pcm_t *pcm, snd_pcm_sw_params_t *params, snd_pcm_uframes_t val) {
    (void)pcm; (void)params;
    g_alsa_mock.sw_start_threshold = (int)val;
    return 0;
}
int snd_pcm_sw_params_set_avail_min(snd_pcm_t *pcm, snd_pcm_sw_params_t *params, snd_pcm_uframes_t val) { (void)pcm; (void)params; (void)val; return 0; }
int snd_pcm_sw_params(snd_pcm_t *pcm, snd_pcm_sw_params_t *params) { (void)pcm; (void)params; return 0; }


int snd_pcm_set_params(snd_pcm_t *pcm,
                       snd_pcm_format_t format,
                       snd_pcm_access_t access,
                       unsigned int channels,
                       unsigned int rate,
                       int soft_resample,
                       unsigned int latency) {
    (void)pcm; (void)format; (void)access; (void)channels; (void)rate; (void)soft_resample; (void)latency;
    if (g_alsa_mock.fail_set_params) return -1;
    g_alsa_mock.state = PcmState::PREPARED;
    return 0;
}

int snd_pcm_close(snd_pcm_t *pcm) {
    (void)pcm;
    g_alsa_mock.state = PcmState::CLOSED;
    return 0; 
}

int snd_pcm_prepare(snd_pcm_t *pcm) {
    (void)pcm;
    g_alsa_mock.state = PcmState::PREPARED;
    return 0;
}

int snd_pcm_drop(snd_pcm_t *pcm) {
    (void)pcm;
    if (g_alsa_mock.state == PcmState::RUNNING) {
        g_alsa_mock.state = PcmState::SETUP;
    }
    return 0;
}

int snd_pcm_drain(snd_pcm_t *pcm) {
    (void)pcm;
    g_alsa_mock.state = PcmState::SETUP;
    return 0;
}

snd_pcm_sframes_t snd_pcm_writei(snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size) {
    (void)pcm; (void)buffer;
    if (g_alsa_mock.state == PcmState::PREPARED) {
        g_alsa_mock.state = PcmState::RUNNING;
    }
    if (g_alsa_mock.simulate_underrun) {
        g_alsa_mock.simulate_underrun = false; // Underrun once
        g_alsa_mock.state = PcmState::SETUP; // XRUN moves to setup essentially for recover
        return -EPIPE; 
    }
    g_alsa_mock.written_frames.push_back((int)size);
    return size;
}

int snd_pcm_recover(snd_pcm_t *pcm, int err, int silent) {
    (void)pcm; (void)err; (void)silent;
    if (g_alsa_mock.recover_fail_count > 0) {
        g_alsa_mock.recover_fail_count--;
        return -1; // Fail recovery
    }
    g_alsa_mock.state = PcmState::PREPARED;
    return 0; // Success recovery
}

size_t snd_pcm_hw_params_sizeof() { return 1024; }
size_t snd_pcm_sw_params_sizeof() { return 1024; }
int snd_pcm_resume(snd_pcm_t *pcm) { (void)pcm; return 0; }
int snd_pcm_pause(snd_pcm_t *pcm, int enable) {
    (void)pcm; (void)enable;
    if (enable) {
        g_alsa_mock.state = PcmState::PAUSED;
    } else {
        if (g_alsa_mock.state == PcmState::PAUSED)
            g_alsa_mock.state = PcmState::RUNNING;
    }
    return 0;
}

// Stubs for other used symbols
const char *snd_strerror(int errnum) {
    (void)errnum;
    return "Mock ALSA Error";
}

// CURL stubs
CURL* curl_easy_init() {
    return reinterpret_cast<CURL*>(0xBEEFCAFE);
}

CURLcode curl_easy_setopt(CURL *curl, CURLoption option, ...) {
    (void)curl;
    va_list arg;
    va_start(arg, option);
    if (option == CURLOPT_URL) {
        const char* url = va_arg(arg, const char*);
        g_curl_mock.requested_urls.push_back(url);
    } else if (option == CURLOPT_USERAGENT) {
        const char* ua = va_arg(arg, const char*);
        g_curl_mock.last_user_agent = ua;
    } else if (option == CURLOPT_WRITEDATA) {
        // We might want to store this to write back data
    }
    va_end(arg);
    return CURLE_OK;
}

CURLcode curl_easy_perform(CURL *curl) {
    (void)curl;
    if (g_curl_mock.requested_urls.empty()) return CURLE_FAILED_INIT;
    std::string url = g_curl_mock.requested_urls.back();
    
    if (g_curl_mock.mock_errors.count(url)) {
        return g_curl_mock.mock_errors[url];
    }
    
    // If we have a mock response, we need to call the callback!
    if (g_curl_mock.mock_responses.count(url)) {
        // Find the callback and userdata set via setopt
        // For simplicity in this mock, we assume the user set WriteCallback
        // and a std::string* as userdata, which is what NewsModule does.
        // In a real mock we'd store these pointers in CurlMockState.
    }
    
    return CURLE_OK;
}

CURLcode curl_easy_getinfo(CURL *curl, CURLINFO info, ...) {
    (void)curl;
    va_list arg;
    va_start(arg, info);
    if (info == CURLINFO_RESPONSE_CODE) {
        long* code = va_arg(arg, long*);
        std::string url = g_curl_mock.requested_urls.back();
        if (g_curl_mock.mock_errors.count(url)) {
             *code = 403; // Mocking error code
        } else {
             *code = 200;
        }
    }
    va_end(arg);
    return CURLE_OK;
}

void curl_easy_cleanup(CURL *curl) { (void)curl; }

const char* curl_easy_strerror(CURLcode error) {
    (void)error;
    return "Mock CURL Error";
}

CURLcode curl_global_init(long flags) { (void)flags; return CURLE_OK; }
void curl_global_cleanup() {}

char* curl_easy_escape(CURL *curl, const char *string, int length) {
    (void)curl; (void)length;
    return strdup(string);
}

void curl_free(void *ptr) {
    free(ptr);
}

} // extern C
