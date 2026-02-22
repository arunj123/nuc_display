#pragma once

#include <string>
#include <vector>
#include <expected>
#include <nlohmann/json.hpp>
#include <memory>

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
    int weather_code;
    std::string description;
    std::string icon_path;
    std::string city;
};

class WeatherModule {
public:
    WeatherModule();
    ~WeatherModule();

    std::expected<WeatherData, WeatherError> fetch_current_weather(float lat, float lon);
    
    // UI Helpers
    std::string get_weather_description(int code);
    std::string get_weather_icon_filename(int code);

    // Rendering
    void render(core::Renderer& renderer, ImageLoader& image_loader, TextRenderer& text_renderer, const WeatherData& data);

private:
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
    
    uint32_t weather_icon_tex_ = 0;
    std::string current_icon_path_;
};

} // namespace nuc_display::modules
