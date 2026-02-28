#pragma once

#include <string>
#include <vector>
#include <optional>
#include <expected>
#include <cstdint>
#include <unordered_map>
#include "modules/stock_module.hpp" // For StockConfig

namespace nuc_display::modules {

enum class ConfigError {
    FileNotFound,
    ParseError,
    GeocodeNetworkError,
    GeocodeParseError
};

struct LocationConfig {
    std::string name;  // Display-only label (e.g. "Hasenbuck, Nürnberg")
    float lat;
    float lon;
};

struct GlobalKeysConfig {
    std::optional<uint16_t> hide_videos;
};

struct StockKeysConfig {
    std::optional<uint16_t> next_stock;
    std::optional<uint16_t> prev_stock;
    std::optional<uint16_t> next_chart;
    std::optional<uint16_t> prev_chart;
};

struct VideoKeysConfig {
    std::optional<uint16_t> next;
    std::optional<uint16_t> prev;
    std::optional<uint16_t> skip_forward;
    std::optional<uint16_t> skip_backward;
};

struct VideoConfig {
    bool enabled = true;
    bool audio_enabled = false;
    std::string audio_device = "default";
    std::vector<std::string> playlists;
    float x = 0.0f, y = 0.0f, w = 1.0f, h = 1.0f;
    float src_x = 0.0f, src_y = 0.0f, src_w = 1.0f, src_h = 1.0f;

    // Key-driven start trigger: 0 = auto, >0 = key code
    uint16_t start_trigger_key = 0;
    std::string start_trigger_name = "auto";

    // Per-playlist navigation keys (all optional)
    VideoKeysConfig keys;
};

struct CameraConfig {
    bool enabled = true;
    std::string device = "";         // e.g. "/dev/video0", "" = auto-detect
    int width = 640;
    int height = 480;
    int fps = 30;
    std::string pixel_format = "MJPG"; // MJPG, YUYV, NV12
    // Destination rect (same convention as VideoConfig)
    float x = 0.0f, y = 0.0f, w = 1.0f, h = 1.0f;
    float src_x = 0.0f, src_y = 0.0f, src_w = 1.0f, src_h = 1.0f;
};

enum class LayoutType {
    Weather,
    Stocks,
    News,
    Video,
    Camera
};

struct LayoutEntry {
    LayoutType type;
    int video_index = -1;   // Only used when type == Video
    int camera_index = -1;  // Only used when type == Camera
};

struct AppConfig {
    LocationConfig location;
    std::vector<StockConfig> stocks;
    std::vector<VideoConfig> videos;
    std::vector<CameraConfig> cameras;
    std::vector<LayoutEntry> layout;  // Draw order: first = behind, last = on top
    GlobalKeysConfig global_keys;
    StockKeysConfig stock_keys;
};

// Key name to Linux KEY_* code mapping
uint16_t key_name_to_code(const std::string& name);
std::string key_code_to_name(uint16_t code);
bool is_valid_key_name(const std::string& name);

struct GeocodeResult {
    float lat;
    float lon;
    std::string resolved_name;
};

class ConfigModule {
public:
    ConfigModule();
    ~ConfigModule();

    std::expected<AppConfig, ConfigError> load_or_create_config(const std::string& filepath);

    // Uses Open-Meteo Geocoding API
    std::expected<GeocodeResult, ConfigError> geocode_address(const std::string& address);

private:
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
    
    // Save to disk
    void save_config(const AppConfig& config, const std::string& filepath);
};

} // namespace nuc_display::modules
