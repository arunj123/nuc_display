#include "modules/input_module.hpp"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <cstring>
#include <poll.h>
#include <optional>
#include <deque>
#include <chrono>
#include <set>
#include <algorithm>
#include <sys/stat.h>
#include <sys/ioctl.h>

namespace nuc_display::modules {

InputModule::InputModule() {
    this->discover_keyboards();
    last_discover_time_ = std::chrono::steady_clock::now();
}

InputModule::~InputModule() {
    this->stop();
    std::lock_guard<std::mutex> lock(this->fd_mutex_);
    for (int fd : this->fds_) {
        close(fd);
    }
}

void InputModule::discover_keyboards() {
    DIR* dir = opendir("/dev/input");
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "event", 5) == 0) {
            std::string path = "/dev/input/" + std::string(entry->d_name);
            int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
            if (fd >= 0) {
                // Check if it has keys
                unsigned long key_bitmask[KEY_MAX / 8 / sizeof(unsigned long) + 1];
                memset(key_bitmask, 0, sizeof(key_bitmask));
                if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bitmask)), key_bitmask) >= 0) {
                    // Check for some common keys (like ESC, SPACE, etc.) to confirm it's a keyboard
                    if (key_bitmask[KEY_ESC / (sizeof(unsigned long) * 8)] & (1UL << (KEY_ESC % (sizeof(unsigned long) * 8)))) {
                        char name[256] = "Unknown";
                        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
                        std::cout << "[Input] Found Keyboard: " << name << " (" << path << ")\n";
                        this->fds_.push_back(fd);
                    } else {
                        close(fd);
                    }
                } else {
                    close(fd);
                }
            }
        }
    }
    closedir(dir);
}

void InputModule::start() {
    if (this->fds_.empty()) {
        std::cerr << "[Input] No keyboard devices found at startup (hot-plug will retry).\n";
    }
    this->running_ = true;
    this->thread_ = std::thread(&InputModule::polling_thread, this);
}

void InputModule::stop() {
    this->running_ = false;
    if (this->thread_.joinable()) {
        this->thread_.join();
    }
}

std::optional<KeyEvent> InputModule::pop_event() {
    std::lock_guard<std::mutex> lock(this->event_mutex_);
    if (this->event_queue_.empty()) return std::nullopt;
    KeyEvent ev = this->event_queue_.front();
    this->event_queue_.pop_front();
    return ev;
}

void InputModule::rediscover_keyboards() {
    // Collect currently tracked fd inodes to avoid duplicates
    std::set<ino_t> existing_inodes;
    {
        std::lock_guard<std::mutex> lock(this->fd_mutex_);
        for (int fd : this->fds_) {
            struct stat st;
            if (fstat(fd, &st) == 0) {
                existing_inodes.insert(st.st_ino);
            }
        }
    }

    DIR* dir = opendir("/dev/input");
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "event", 5) == 0) {
            std::string path = "/dev/input/" + std::string(entry->d_name);
            
            // Check inode before opening to avoid duplicates
            struct stat st;
            if (stat(path.c_str(), &st) == 0 && existing_inodes.count(st.st_ino)) {
                continue; // Already tracked
            }

            int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
            if (fd >= 0) {
                unsigned long key_bitmask[KEY_MAX / 8 / sizeof(unsigned long) + 1];
                memset(key_bitmask, 0, sizeof(key_bitmask));
                if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bitmask)), key_bitmask) >= 0) {
                    if (key_bitmask[KEY_ESC / (sizeof(unsigned long) * 8)] & (1UL << (KEY_ESC % (sizeof(unsigned long) * 8)))) {
                        char name[256] = "Unknown";
                        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
                        std::cout << "[Input] Hot-plug: Found keyboard: " << name << " (" << path << ")\n";
                        {
                            std::lock_guard<std::mutex> lock(this->fd_mutex_);
                            this->fds_.push_back(fd);
                        }
                        continue; // Don't close — it's now tracked
                    }
                }
                close(fd);
            }
        }
    }
    closedir(dir);
}

void InputModule::polling_thread() {
    while (this->running_) {
        // Build pollfds from current fds
        std::vector<struct pollfd> pollfds;
        {
            std::lock_guard<std::mutex> lock(this->fd_mutex_);
            for (int fd : this->fds_) {
                pollfds.push_back({fd, POLLIN, 0});
            }
        }

        if (pollfds.empty()) {
            // No keyboards — sleep and try rediscovery
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_discover_time_).count() >= rediscover_interval_sec_) {
                rediscover_keyboards();
                last_discover_time_ = now;
            }
            continue;
        }

        int ret = poll(pollfds.data(), pollfds.size(), 100); // 100ms timeout
        if (ret > 0) {
            std::vector<int> stale_fds;
            for (auto& pfd : pollfds) {
                if (pfd.revents & (POLLHUP | POLLERR)) {
                    std::cout << "[Input] Keyboard disconnected (fd=" << pfd.fd << ")\n";
                    stale_fds.push_back(pfd.fd);
                    continue;
                }
                if (pfd.revents & POLLIN) {
                    struct input_event ev;
                    while (read(pfd.fd, &ev, sizeof(ev)) > 0) {
                        if (ev.type == EV_KEY) {
                            {
                                std::lock_guard<std::mutex> lock(this->event_mutex_);
                                this->event_queue_.push_back({ev.code, ev.value});
                            }
                            
                            std::string state = (ev.value == 1) ? "DOWN" : (ev.value == 0 ? "UP" : "REPEAT");
                            std::cout << "[Input] Key Press: Code " << ev.code << " [" << state << "]\n";
                        }
                    }
                }
            }

            // Remove stale fds
            if (!stale_fds.empty()) {
                std::lock_guard<std::mutex> lock(this->fd_mutex_);
                for (int sfd : stale_fds) {
                    close(sfd);
                    this->fds_.erase(std::remove(this->fds_.begin(), this->fds_.end(), sfd), this->fds_.end());
                }
            }
        }

        // Periodic re-discovery
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_discover_time_).count() >= rediscover_interval_sec_) {
            rediscover_keyboards();
            last_discover_time_ = now;
        }
    }
}

} // namespace nuc_display::modules
