#pragma once

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <optional>
#include <deque>
#include <chrono>
#include <linux/input.h>

namespace nuc_display::modules {

struct KeyEvent {
    uint16_t code;
    int value; // 1 for down, 0 for up, 2 for repeat
};

class InputModule {
public:
    InputModule();
    ~InputModule();

    void start();
    void stop();

    std::optional<KeyEvent> pop_event();

private:
    void polling_thread();
    void discover_keyboards();
    void rediscover_keyboards();

    std::vector<int> fds_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    
    std::mutex event_mutex_;
    std::deque<KeyEvent> event_queue_;

    std::mutex fd_mutex_;
    std::chrono::steady_clock::time_point last_discover_time_;
    static constexpr int rediscover_interval_sec_ = 5;
};

} // namespace nuc_display::modules
