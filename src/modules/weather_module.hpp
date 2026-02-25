#pragma once

#include <string>
#include <vector>
#include <expected>
#include <nlohmann/json.hpp>
#include <memory>
#include <curl/curl.h>

namespace nuc_display::core { class Renderer; }

namespace nuc_display::modules { 

class ImageLoader; 
class TextRenderer; 

enum class WeatherError {
    NetworkError,
    ParseError,
    InvalidData,
    IconNotFound
};

struct WeatherData {
    float temperature;
    float humidity;
    float wind_speed;
    float visibility;
    float feels_like;
    float uv_index;
    int weather_code;
    std::string description;
    std::string icon_path;
    std::string city;
    std::string sunrise;
    std::string sunset;
};

class WeatherModule {
public:
    WeatherModule();
    ~WeatherModule();

    std::expected<WeatherData, WeatherError> fetch_current_weather(float lat, float lon, const std::string& location_name);
    
    // UI Helpers
    std::string get_weather_description(int code);
    std::string get_weather_icon_filename(int code);

    // Rendering
    void render(core::Renderer& renderer, TextRenderer& text_renderer, const WeatherData& data, double time_sec);

private:
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
    CURL* curl_handle_ = nullptr;
};

} // namespace nuc_display::modules
