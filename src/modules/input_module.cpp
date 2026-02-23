#include "modules/input_module.hpp"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <cstring>
#include <poll.h>

namespace nuc_display::modules {

InputModule::InputModule() {
    this->discover_keyboards();
}

InputModule::~InputModule() {
    this->stop();
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
        std::cerr << "[Input] No keyboard devices found or permission denied (check 'input' group).\n";
        return;
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

void InputModule::polling_thread() {
    std::vector<struct pollfd> pollfds;
    for (int fd : this->fds_) {
        pollfds.push_back({fd, POLLIN, 0});
    }

    while (this->running_) {
        int ret = poll(pollfds.data(), pollfds.size(), 100); // 100ms timeout
        if (ret > 0) {
            for (auto& pfd : pollfds) {
                if (pfd.revents & POLLIN) {
                    struct input_event ev;
                    while (read(pfd.fd, &ev, sizeof(ev)) > 0) {
                        if (ev.type == EV_KEY) {
                            std::string state = (ev.value == 1) ? "DOWN" : (ev.value == 0 ? "UP" : "REPEAT");
                            std::cout << "[Input] Key Press: Code " << ev.code << " [" << state << "]\n";
                        }
                    }
                }
            }
        }
    }
}

} // namespace nuc_display::modules
