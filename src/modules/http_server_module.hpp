#pragma once

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

namespace nuc_display::modules {

class InputModule;

struct QrCodeImage {
    std::vector<uint8_t> rgba_pixels;
    int size = 0;
};

class HttpServerModule {
public:
    HttpServerModule(InputModule* input_module, const std::string& config_path, std::atomic<bool>& reload_flag, int port = 8080);
    ~HttpServerModule();

    void start();
    void stop();

    std::string get_web_address() const;
    std::string get_ip_address() const;
    int get_port() const;

    bool has_qr_code_updates() const { return qr_code_updated_.load(); }
    QrCodeImage get_qr_code_image(); // Clears update flag

private:
    void listen_loop();
    void generate_qr_code(const std::string& text);
    std::string get_local_ip() const;

    InputModule* input_module_;
    std::string config_path_;
    std::atomic<bool>& reload_flag_;
    int port_;
    std::string ip_address_;
    std::string web_address_;

    int server_fd_ = -1;
    std::thread thread_;
    std::atomic<bool> running_{false};

    std::atomic<bool> qr_code_updated_{false};
    std::mutex qr_mutex_;
    mutable std::mutex ip_mutex_;
    QrCodeImage qr_image_;
};

} // namespace nuc_display::modules
