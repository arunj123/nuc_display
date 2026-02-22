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

struct AppConfig {
    LocationConfig location;
    std::vector<StockConfig> stocks;
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
