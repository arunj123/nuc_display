#pragma once

#include <string>
#include <vector>
#include <expected>
#include "modules/stock_module.hpp" // For StockConfig

namespace nuc_display::modules {

enum class ConfigError {
    FileNotFound,
    ParseError,
    GeocodeNetworkError,
    GeocodeParseError
};

struct LocationConfig {
    std::string address;
    float lat;
    float lon;
};

struct VideoConfig {
    bool enabled;
    bool audio_enabled;
    std::string audio_device;
    std::vector<std::string> playlists;
    float x, y, w, h;
    float src_x, src_y, src_w, src_h; // The "from" region (normalized 0.0 to 1.0)
};

struct AppConfig {
    LocationConfig location;
    std::vector<StockConfig> stocks;
    VideoConfig video;
};

class ConfigModule {
public:
    ConfigModule();
    ~ConfigModule();

    std::expected<AppConfig, ConfigError> load_or_create_config(const std::string& filepath);

private:
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
    
    // Uses Open-Meteo Geocoding API
    std::expected<std::pair<float, float>, ConfigError> geocode_address(const std::string& address);
    
    // Save to disk
    void save_config(const AppConfig& config, const std::string& filepath);
};

} // namespace nuc_display::modules
