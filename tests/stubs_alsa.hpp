#ifndef STUBS_ALSA_HPP
#define STUBS_ALSA_HPP

#include <alsa/asoundlib.h>
#include <vector>

namespace nuc_display::tests::mock {

// Global mock state
struct AlsaMockState {
    bool fail_open = false;
    bool fail_set_params = false;
    bool simulate_underrun = false;
    int recover_fail_count = 0;
    
    std::vector<int> written_frames;
    
    void reset() {
        fail_open = false;
        fail_set_params = false;
        simulate_underrun = false;
        recover_fail_count = 0;
        written_frames.clear();
    }
};

extern AlsaMockState g_alsa_mock;

} // namespace nuc_display::tests::mock

#endif // STUBS_ALSA_HPP
