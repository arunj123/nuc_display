#include "weather_module.hpp"
#include "core/renderer.hpp"
#include "modules/image_loader.hpp"
#include "modules/text_renderer.hpp"
#include <curl/curl.h>
#include <iostream>
#include <sstream>

namespace nuc_display::modules {

WeatherModule::WeatherModule() {
    curl_global_init(CURL_GLOBAL_ALL);
}

WeatherModule::~WeatherModule() {
    curl_global_cleanup();
}

size_t WeatherModule::WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::expected<WeatherData, WeatherError> WeatherModule::fetch_current_weather(float lat, float lon) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if (!curl) return std::unexpected(WeatherError::NetworkError);

    std::stringstream url;
    url << "https://api.open-meteo.com/v1/forecast?latitude=" << lat 
        << "&longitude=" << lon 
        << "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m";

    curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    
    // Some systems need explicit CA cert path or disabling verify if certs are missing
    // For now we'll assume system certs are okay
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "CURL Error: " << curl_easy_strerror(res) << "\n";
        return std::unexpected(WeatherError::NetworkError);
    }

    try {
        auto json = nlohmann::json::parse(readBuffer);
        auto current = json["current"];

        WeatherData data;
        data.temperature = current["temperature_2m"];
        data.humidity    = current["relative_humidity_2m"];
        data.wind_speed  = current["wind_speed_10m"];
        data.weather_code = current["weather_code"];
        data.description = get_weather_description(data.weather_code);
        data.icon_path   = get_weather_icon_filename(data.weather_code);
        data.city        = "Home Sensor"; // Default for now

        return data;
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "JSON Parse Error: " << e.what() << "\n";
        return std::unexpected(WeatherError::ParseError);
    }
}

std::string WeatherModule::get_weather_description(int code) {
    switch (code) {
        case 0:  return "Clear sky";
        case 1:  case 2: case 3: return "Mainly clear, partly cloudy, and overcast";
        case 45: case 48: return "Fog and depositing rime fog";
        case 51: case 53: case 55: return "Drizzle: Light, moderate, and dense intensity";
        case 56: case 57: return "Freezing Drizzle: Light and heavy intensity";
        case 61: case 63: case 65: return "Rain: Slight, moderate and heavy intensity";
        case 66: case 67: return "Freezing Rain: Light and heavy intensity";
        case 71: case 73: case 75: return "Snow fall: Slight, moderate, and heavy intensity";
        case 77: return "Snow grains";
        case 80: case 81: case 82: return "Rain showers: Slight, moderate, and violent";
        case 85: case 86: return "Snow showers slight and heavy";
        case 95: return "Thunderstorm: Slight or moderate";
        case 96: case 99: return "Thunderstorm with slight and heavy hail";
        default: return "Unknown";
    }
}

std::string WeatherModule::get_weather_icon_filename(int code) {
    // We will generate these icons or find them
    switch (code) {
        case 0:  return "assets/weather/clear.png";
        case 1:  case 2: case 3: return "assets/weather/cloudy.png";
        case 45: case 48: return "assets/weather/fog.png";
        case 51: case 53: case 55: return "assets/weather/drizzle.png";
        case 61: case 63: case 65: return "assets/weather/rain.png";
        case 71: case 73: case 75: return "assets/weather/snow.png";
        case 95: case 96: case 99: return "assets/weather/storm.png";
        default: return "assets/weather/unknown.png";
    }
}

void WeatherModule::render(core::Renderer& renderer, ImageLoader& image_loader, TextRenderer& text_renderer, const WeatherData& data) {
    // 1. Handle Icon
    if (data.icon_path != current_icon_path_) {
        if (weather_icon_tex_) renderer.delete_texture(weather_icon_tex_);
        
        if (auto res = image_loader.load(data.icon_path); res) {
            weather_icon_tex_ = renderer.create_texture(image_loader.get_rgba_data().data(), image_loader.width(), image_loader.height(), image_loader.channels());
            current_icon_path_ = data.icon_path;
        }
    }

    // 2. Clear Screen background (dark sleek grey)
    renderer.clear(0.05f, 0.05f, 0.07f, 1.0f);

    // 3. Draw Icon (Left Side)
    if (weather_icon_tex_) {
        // x, y, w, h in 0..1 space. Center vertically.
        renderer.draw_quad(weather_icon_tex_, 0.05f, 0.25f, 0.4f, 0.5f);
    }

    // 4. Draw Temperature
    std::stringstream temp_ss;
    temp_ss.precision(1);
    temp_ss << std::fixed << data.temperature << "°C";
    
    text_renderer.set_pixel_size(0, 140); // Set size BEFORE shaping
    auto temp_glyphs = text_renderer.shape_text(temp_ss.str());
    if (temp_glyphs) {
        renderer.draw_text(temp_glyphs.value(), 0.5f, 0.35f, 1.0f);
    }

    // 5. Draw Description
    text_renderer.set_pixel_size(0, 56);
    auto desc_glyphs = text_renderer.shape_text(data.description);
    if (desc_glyphs) {
        renderer.draw_text(desc_glyphs.value(), 0.5f, 0.55f, 1.0f);
    }

    // 6. Draw Details (Humidity, Wind)
    std::stringstream detail_ss;
    detail_ss << "Humidity: " << (int)data.humidity << "% | Wind: " << data.wind_speed << " km/h";
    
    text_renderer.set_pixel_size(0, 36);
    auto detail_glyphs = text_renderer.shape_text(detail_ss.str());
    if (detail_glyphs) {
        renderer.draw_text(detail_glyphs.value(), 0.5f, 0.68f, 1.0f);
    }
}

} // namespace nuc_display::modules
