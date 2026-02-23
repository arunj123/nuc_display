#include "config_module.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

namespace nuc_display::modules {

ConfigModule::ConfigModule() {
    curl_global_init(CURL_GLOBAL_ALL);
}

ConfigModule::~ConfigModule() {
    curl_global_cleanup();
}

size_t ConfigModule::WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::expected<std::pair<float, float>, ConfigError> ConfigModule::geocode_address(const std::string& address) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if (!curl) return std::unexpected(ConfigError::GeocodeNetworkError);

    // URL encode the address
    char* encoded_addr = curl_easy_escape(curl, address.c_str(), address.length());
    std::string url = "https://geocoding-api.open-meteo.com/v1/search?name=" + std::string(encoded_addr) + "&count=1&language=en&format=json";
    curl_free(encoded_addr);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    std::cout << "Geocoding address: " << address << "...\n";
    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "Geocode CURL Error: " << curl_easy_strerror(res) << "\n";
        return std::unexpected(ConfigError::GeocodeNetworkError);
    }

    try {
        auto json = nlohmann::json::parse(readBuffer);
        if (json.contains("results") && json["results"].is_array() && !json["results"].empty()) {
            float lat = json["results"][0]["latitude"];
            float lon = json["results"][0]["longitude"];
            std::cout << "Resolved to Lat: " << lat << ", Lon: " << lon << "\n";
            return std::make_pair(lat, lon);
        } else {
            std::cerr << "Geocode returned no results for: " << address << " (using defaults)\n";
            return std::unexpected(ConfigError::GeocodeParseError);
        }
    } catch (const std::exception& e) {
        std::cerr << "Geocode JSON Parse Error: " << e.what() << "\n";
        return std::unexpected(ConfigError::GeocodeParseError);
    }
}

void ConfigModule::save_config(const AppConfig& config, const std::string& filepath) {
    nlohmann::json j;
    
    j["location"]["address"] = config.location.address;
    j["location"]["lat"] = config.location.lat;
    j["location"]["lon"] = config.location.lon;

    nlohmann::json stocks = nlohmann::json::array();
    for (const auto& s : config.stocks) {
        nlohmann::json sj;
        sj["symbol"] = s.symbol;
        sj["name"] = s.name;
        sj["currency_symbol"] = s.currency_symbol;
        stocks.push_back(sj);
    }
    j["stocks"] = stocks;

    nlohmann::json videos = nlohmann::json::array();
    for (const auto& v : config.videos) {
        nlohmann::json vj;
        vj["enabled"] = v.enabled;
        vj["audio_enabled"] = v.audio_enabled;
        vj["audio_device"] = v.audio_device;
        vj["playlists"] = v.playlists;
        vj["x"] = v.x;
        vj["y"] = v.y;
        vj["w"] = v.w;
        vj["h"] = v.h;
        vj["src_x"] = v.src_x;
        vj["src_y"] = v.src_y;
        vj["src_w"] = v.src_w;
        vj["src_h"] = v.src_h;
        videos.push_back(vj);
    }
    j["videos"] = videos;
    std::ofstream out(filepath);
    if (out.is_open()) {
        out << j.dump(4);
    }
}

std::expected<AppConfig, ConfigError> ConfigModule::load_or_create_config(const std::string& filepath) {
    AppConfig config;
    bool needs_save = false;

    std::ifstream in(filepath);
    if (!in.is_open()) {
        std::cout << "Config not found at " << filepath << ". Generating default config.\n";
        // Default Config
        config.location.address = "Hasenbuk, Nürnberg, Germany";
        
        // Dynamic geocode
        auto coords = geocode_address(config.location.address);
        if (coords) {
            config.location.lat = coords.value().first;
            config.location.lon = coords.value().second;
        } else {
            // Fallback
            config.location.lat = 49.4521f;
            config.location.lon = 11.0767f;
        }

        // Default Stocks
        config.stocks = {
            {"^IXIC", "NASDAQ", "$"},
            {"^GSPC", "S&P 500", "$"},
            {"^NSEI", "NIFTY 50", "₹"},
            {"^BSESN", "BSE SENSEX", "₹"},
            {"APC.F", "Apple", "€"},
            {"MSF.F", "Microsoft", "€"},
            {"NVD.F", "Nvidia", "€"},
            {"AMZ.F", "Amazon", "€"},
            {"FB2A.F", "Meta", "€"},
            {"ABEA.F", "Alphabet", "€"},
            {"TL0.F", "Tesla", "€"}
        };

        // Default Video Config
        VideoConfig v;
        v.enabled = true;
        v.audio_enabled = false;
        v.audio_device = "default";
        v.playlists = {"tests/sample.mp4"};
        v.x = 0.70f;
        v.y = 0.03f;
        v.w = 0.25f;
        v.h = 0.20f;
        v.src_x = 0.0f;
        v.src_y = 0.0f;
        v.src_w = 1.0f;
        v.src_h = 1.0f;
        config.videos.push_back(v);

        needs_save = true;
    } else {
        try {
            nlohmann::json j;
            in >> j;

            if (j.contains("location")) {
                config.location.address = j["location"].value("address", "Hasenbuk, Nürnberg, Germany");
                
                // If loaded lat/lon is exactly 0, try to geocode
                config.location.lat = j["location"].value("lat", 0.0f);
                config.location.lon = j["location"].value("lon", 0.0f);
                
                if (config.location.lat == 0.0f && config.location.lon == 0.0f) {
                    auto coords = geocode_address(config.location.address);
                    if (coords) {
                        config.location.lat = coords.value().first;
                        config.location.lon = coords.value().second;
                        needs_save = true;
                    }
                }
            }

            if (j.contains("stocks") && j["stocks"].is_array()) {
                for (const auto& item : j["stocks"]) {
                    StockConfig s;
                    s.symbol = item.value("symbol", "");
                    s.name = item.value("name", "");
                    s.currency_symbol = item.value("currency_symbol", "$");
                    if (!s.symbol.empty()) {
                        config.stocks.push_back(s);
                    }
                }
            } else {
                needs_save = true; // Repair missing array
            }
            
            auto parse_video = [](const nlohmann::json& video_json) {
                VideoConfig v;
                v.enabled = video_json.value("enabled", true);
                v.audio_enabled = video_json.value("audio_enabled", false);
                v.audio_device = video_json.value("audio_device", "default");
                
                if (video_json.contains("playlists") && video_json["playlists"].is_array()) {
                    for (const auto& item : video_json["playlists"]) {
                        if (item.is_string()) v.playlists.push_back(item);
                    }
                }
                
                v.x = video_json.value("x", 0.0f);
                v.y = video_json.value("y", 0.0f);
                v.w = video_json.value("w", 1.0f);
                v.h = video_json.value("h", 1.0f);
                v.src_x = video_json.value("src_x", 0.0f);
                v.src_y = video_json.value("src_y", 0.0f);
                v.src_w = video_json.value("src_w", 1.0f);
                v.src_h = video_json.value("src_h", 1.0f);
                return v;
            };

            if (j.contains("videos") && j["videos"].is_array()) {
                for (const auto& v_json : j["videos"]) {
                    config.videos.push_back(parse_video(v_json));
                }
            } else if (j.contains("video")) {
                // Fallback for single video
                config.videos.push_back(parse_video(j["video"]));
            } else {
                // Default if none
                VideoConfig v;
                v.enabled = true;
                v.audio_enabled = false;
                v.playlists = {"tests/sample.mp4"};
                v.x = 0.70f; v.y = 0.03f; v.w = 0.25f; v.h = 0.20f;
                config.videos.push_back(v);
                needs_save = true;
            }
            
        } catch (const nlohmann::json::parse_error& e) {
            std::cerr << "Config Parse Error: " << e.what() << "\n";
            return std::unexpected(ConfigError::ParseError);
        } catch (const std::exception& e) {
            std::cerr << "Config Parse Error: " << e.what() << "\n";
            return std::unexpected(ConfigError::ParseError);
        }
    }

    if (needs_save) {
        save_config(config, filepath);
    }

    return config;
}

} // namespace nuc_display::modules
