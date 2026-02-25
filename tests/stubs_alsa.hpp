#ifndef STUBS_ALSA_HPP
#define STUBS_ALSA_HPP

#include <alsa/asoundlib.h>
#include <curl/curl.h>
#include <vector>
#include <string>
#include <map>

namespace nuc_display::tests::mock {

enum class PcmState {
    OPEN,
    SETUP,
    PREPARED,
    RUNNING,
    CLOSED
};

// Global mock state
struct AlsaMockState {
    bool fail_open = false;
    bool fail_set_params = false;
    bool simulate_underrun = false;
    int recover_fail_count = 0;
    
    PcmState state = PcmState::CLOSED;
    bool hw_params_any_called = false;
    int sw_start_threshold = 0;
    
    std::vector<int> written_frames;
    
    void reset() {
        fail_open = false;
        fail_set_params = false;
        simulate_underrun = false;
        recover_fail_count = 0;
        state = PcmState::CLOSED;
        hw_params_any_called = false;
        sw_start_threshold = 0;
        written_frames.clear();
    }
};

extern AlsaMockState g_alsa_mock;

// CURL mock state
struct CurlMockState {
    std::string last_user_agent;
    std::vector<std::string> requested_urls;
    std::map<std::string, std::string> mock_responses;
    std::map<std::string, CURLcode> mock_errors;
    
    void reset() {
        last_user_agent.clear();
        requested_urls.clear();
        mock_responses.clear();
        mock_errors.clear();
    }
};

extern CurlMockState g_curl_mock;

} // namespace nuc_display::tests::mock

#endif // STUBS_ALSA_HPP
