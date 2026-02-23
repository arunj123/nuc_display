#include "weather_module.hpp"
#include "core/renderer.hpp"
#include "modules/image_loader.hpp"
#include "modules/text_renderer.hpp"
#include <curl/curl.h>
#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>

namespace nuc_display::modules {

// Helper to wrap text after a certain number of characters
static std::vector<std::string> wrap_text(const std::string& text, size_t max_chars) {
    std::vector<std::string> lines;
    std::istringstream words(text);
    std::string word, current_line;
    while (words >> word) {
        if (current_line.length() + word.length() + 1 > max_chars && !current_line.empty()) {
            lines.push_back(current_line);
            current_line = word;
        } else {
            if (!current_line.empty()) current_line += " ";
            current_line += word;
        }
    }
    if (!current_line.empty()) lines.push_back(current_line);
    return lines;
}

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
        << "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,visibility,apparent_temperature,uv_index"
        << "&daily=sunrise,sunset&timezone=auto";

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
        auto daily = json["daily"];

        WeatherData data;
        data.temperature = current.value("temperature_2m", 0.0f);
        data.humidity    = current.value("relative_humidity_2m", 0.0f);
        data.wind_speed  = current.value("wind_speed_10m", 0.0f);
        data.visibility  = current.value("visibility", 0.0f);
        data.feels_like  = current.value("apparent_temperature", 0.0f);
        data.uv_index    = current.value("uv_index", 0.0f);
        data.weather_code = current.value("weather_code", 0);
        data.description = get_weather_description(data.weather_code);
        data.icon_path   = get_weather_icon_filename(data.weather_code);
        data.city        = "Home"; // Default from config
        
        if (daily.contains("sunrise") && daily["sunrise"].is_array() && !daily["sunrise"].empty()) {
            std::string sr = daily["sunrise"][0];
            if (sr.length() >= 5) data.sunrise = sr.substr(sr.length() - 5);
        }
        if (daily.contains("sunset") && daily["sunset"].is_array() && !daily["sunset"].empty()) {
            std::string ss = daily["sunset"][0];
            if (ss.length() >= 5) data.sunset = ss.substr(ss.length() - 5);
        }

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

void WeatherModule::render(core::Renderer& renderer, TextRenderer& text_renderer, const WeatherData& data, double time_sec) {
    // 1. Clear Screen background (dark sleek grey)
    renderer.clear(0.05f, 0.05f, 0.07f, 1.0f);

    // =========================================================
    // GRID LAYOUT
    // Left column:  x = 0.03 to 0.39  (weather, info, news)
    // Right column: x = 0.42 to 0.97  (stocks)
    // Separator:    x = 0.40
    // =========================================================
    float lx = 0.03f;              // left margin
    float left_col_right = 0.39f;  // right edge of left column
    float left_w = left_col_right - lx;  // 0.36
    float sep_x = 0.405f;          // separator line x
    float aspect = (float)renderer.width() / renderer.height();

    // --- Draw Vertical Separator Line ---
    std::vector<float> sep_pts = { sep_x, 0.03f, sep_x, 0.97f };
    renderer.draw_line_strip(sep_pts, 0.2f, 0.2f, 0.25f, 0.6f, 1.0f);

    // =========================================================
    // ROW 1: Time (left) & Temperature (right)   (y = 0.04 - 0.12)
    // =========================================================
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    struct tm *parts = std::localtime(&now_c);

    // Time (left)
    std::stringstream time_ss;
    time_ss << std::put_time(parts, "%H:%M");
    text_renderer.set_pixel_size(0, 95);
    if (auto glyphs = text_renderer.shape_text(time_ss.str())) {
        renderer.draw_text(glyphs.value(), lx, 0.11f, 1.0f);
    }

    // Temperature (right-aligned in left column)
    std::stringstream temp_ss;
    temp_ss << std::fixed << std::setprecision(1) << data.temperature << "\xC2\xB0" << "C";
    float temp_w = 0.0f;
    if (auto glyphs = text_renderer.shape_text(temp_ss.str())) {
        for (const auto& g : glyphs.value()) temp_w += g.advance / (float)renderer.width();
        float temp_x = left_col_right - temp_w - 0.02f;
        renderer.draw_text(glyphs.value(), temp_x, 0.11f, 1.0f);
    }

    // =========================================================
    // ROW 2: Date & City (centered)    (y = 0.13 - 0.18)
    // =========================================================
    std::stringstream date_ss;
    date_ss << std::put_time(parts, "%a, %b %d") << " | " << data.city;
    text_renderer.set_pixel_size(0, 24);
    if (auto glyphs = text_renderer.shape_text(date_ss.str())) {
        float date_w = 0.0f;
        for (const auto& g : glyphs.value()) date_w += g.advance / (float)renderer.width();
        float date_x = lx + (left_w - date_w) / 2.0f;
        renderer.draw_text(glyphs.value(), date_x, 0.16f, 1.0f, 0.6f, 0.6f, 0.6f, 1.0f);
    }

    // =========================================================
    // ROW 3: Weather Icon           (y = 0.19 - 0.54)
    // =========================================================
    float icon_h = 0.40f; // Enlarged Material 3D Icon
    float icon_w = icon_h / aspect; // Enforces perfect square on 16:9 screen
    float icon_x = lx + (left_w - icon_w) / 2.0f; // Center horizontally
    
    // Day/Night logic based on sunrise/sunset
    bool is_night = false;
    if (data.sunrise.length() == 5 && data.sunset.length() == 5) {
        try {
            int now_m = parts->tm_hour * 60 + parts->tm_min;
            int rise_m = std::stoi(data.sunrise.substr(0,2)) * 60 + std::stoi(data.sunrise.substr(3,2));
            int set_m = std::stoi(data.sunset.substr(0,2)) * 60 + std::stoi(data.sunset.substr(3,2));
            if (now_m < rise_m || now_m > set_m) is_night = true;
        } catch(...) {}
    }
    
    renderer.draw_animated_weather(data.weather_code, icon_x, 0.17f, icon_w, icon_h, time_sec, is_night);


    // =========================================================
    // ROW 4: Description, Warnings, Tip  (y = 0.55 - 0.68)
    // =========================================================
    float text_y = 0.55f;
    text_renderer.set_pixel_size(0, 26);
    auto desc_lines = wrap_text(data.description, 52);
    for (const auto& line : desc_lines) {
        if (auto glyphs = text_renderer.shape_text(line)) {
            renderer.draw_text(glyphs.value(), lx, text_y, 1.0f);
        }
        text_y += 0.035f;
    }

    // Warnings Logic (Color Coded)
    text_renderer.set_pixel_size(0, 20);
    // 1. Glatteis / Black Ice Warning (Temp < 3°C + Rain/Snow)
    if (data.temperature < 3.0f && data.weather_code >= 51 && data.weather_code <= 86) {
        if (auto glyphs = text_renderer.shape_text("WARNING: Glatteis / Ice possible!")) {
            renderer.draw_text(glyphs.value(), lx, text_y, 1.0f, 1.0f, 0.4f, 0.2f, 1.0f); // Bright Red/Orange
            text_y += 0.030f;
        }
    }
    // 2. High UV Index Warning
    else if (data.uv_index >= 6.0f) {
        std::stringstream uv_warn;
        uv_warn << "WARNING: High UV Index (" << std::fixed << std::setprecision(1) << data.uv_index << ")!";
        if (auto glyphs = text_renderer.shape_text(uv_warn.str())) {
            renderer.draw_text(glyphs.value(), lx, text_y, 1.0f, 1.0f, 0.6f, 0.2f, 1.0f); // Orange
            text_y += 0.030f;
        }
    }
    // 3. Storm Warning
    else if (data.weather_code >= 95) {
        if (auto glyphs = text_renderer.shape_text("WARNING: Heavy Thunderstorms!")) {
            renderer.draw_text(glyphs.value(), lx, text_y, 1.0f, 0.8f, 0.3f, 0.8f, 1.0f); // Purple
            text_y += 0.030f;
        }
    }

    // Recommendation tip
    std::string recommendation = "Enjoy the weather!";
    switch (data.weather_code) {
        case 0: case 1: case 2: 
            if (data.temperature < 15.0f) {
                recommendation = "Clear but chilly! Wear a jacket.";
            } else {
                recommendation = "Great day! Wear sunglasses."; 
            }
            break;
        case 3: case 45: case 48: recommendation = "A bit gloomy. Light jacket."; break;
        case 51: case 53: case 55: case 56: case 57: recommendation = "Drizzling. Bring a light coat."; break;
        case 61: case 63: case 65: case 66: case 67: recommendation = "Raining! Don't forget your umbrella."; break;
        case 71: case 73: case 75: case 77: case 85: case 86: recommendation = "Snowing! Warm jacket & gloves."; break;
        case 80: case 81: case 82: recommendation = "Showers. Keep an umbrella handy."; break;
        case 95: case 96: case 99: recommendation = "Thunderstorms. Stay indoors."; break;
    }
    
    if (auto glyphs = text_renderer.shape_text("Tip: " + recommendation)) {
        renderer.draw_text(glyphs.value(), lx, text_y, 1.0f, 0.4f, 0.8f, 1.0f, 1.0f);
    }
    text_y += 0.045f;

    // =========================================================
    // ROW 5: Weather Metrics Grid   (y = ~0.66 - 0.78)
    // =========================================================
    text_renderer.set_pixel_size(0, 18);
    float col_1_x = lx;
    float col_2_x = lx + 0.20f;

    // Row 1: Wind & Humidity
    std::stringstream wind_ss; wind_ss << "Wind: " << std::fixed << std::setprecision(1) << data.wind_speed << " km/h";
    if (auto glyphs = text_renderer.shape_text(wind_ss.str())) {
        renderer.draw_text(glyphs.value(), col_1_x, text_y, 1.0f, 0.5f, 0.5f, 0.5f, 1.0f);
    }
    std::stringstream hum_ss; hum_ss << "Humidity: " << (int)data.humidity << "%";
    if (auto glyphs = text_renderer.shape_text(hum_ss.str())) {
        renderer.draw_text(glyphs.value(), col_2_x, text_y, 1.0f, 0.5f, 0.5f, 0.5f, 1.0f);
    }
    text_y += 0.03f;

    // Row 2: Visibility & Feels Like
    std::stringstream vis_ss; vis_ss << "Vis: " << std::fixed << std::setprecision(1) << (data.visibility / 1000.0f) << " km";
    if (auto glyphs = text_renderer.shape_text(vis_ss.str())) {
        renderer.draw_text(glyphs.value(), col_1_x, text_y, 1.0f, 0.5f, 0.5f, 0.5f, 1.0f);
    }
    std::stringstream app_ss; app_ss << "Feels: " << std::fixed << std::setprecision(1) << data.feels_like << "\xC2\xB0" << "C";
    if (auto glyphs = text_renderer.shape_text(app_ss.str())) {
        renderer.draw_text(glyphs.value(), col_2_x, text_y, 1.0f, 0.5f, 0.5f, 0.5f, 1.0f);
    }
    text_y += 0.03f;
    
    // Row 3: UV Index & Sunrise/Sunset
    std::stringstream uv_ss; uv_ss << "UV Index: " << std::fixed << std::setprecision(1) << data.uv_index;
    if (auto glyphs = text_renderer.shape_text(uv_ss.str())) {
        renderer.draw_text(glyphs.value(), col_1_x, text_y, 1.0f, 0.5f, 0.5f, 0.5f, 1.0f);
    }
    std::stringstream sun_ss;
    sun_ss << "Rise " << (data.sunrise.empty() ? "--:--" : data.sunrise) 
           << " | Set " << (data.sunset.empty() ? "--:--" : data.sunset);
    if (auto glyphs = text_renderer.shape_text(sun_ss.str())) {
        renderer.draw_text(glyphs.value(), col_2_x, text_y, 1.0f, 0.9f, 0.7f, 0.3f, 1.0f);
    }
}

} // namespace nuc_display::modules
