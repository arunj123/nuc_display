#include "config_module.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <linux/input-event-codes.h>

namespace nuc_display::modules {

// --- Key Name <-> Code Mapping ---

static const std::unordered_map<std::string, uint16_t>& get_key_map() {
    static const std::unordered_map<std::string, uint16_t> map = {
        // Letters
        {"a", KEY_A}, {"b", KEY_B}, {"c", KEY_C}, {"d", KEY_D}, {"e", KEY_E},
        {"f", KEY_F}, {"g", KEY_G}, {"h", KEY_H}, {"i", KEY_I}, {"j", KEY_J},
        {"k", KEY_K}, {"l", KEY_L}, {"m", KEY_M}, {"n", KEY_N}, {"o", KEY_O},
        {"p", KEY_P}, {"q", KEY_Q}, {"r", KEY_R}, {"s", KEY_S}, {"t", KEY_T},
        {"u", KEY_U}, {"v", KEY_V}, {"w", KEY_W}, {"x", KEY_X}, {"y", KEY_Y},
        {"z", KEY_Z},
        // Numbers
        {"0", KEY_0}, {"1", KEY_1}, {"2", KEY_2}, {"3", KEY_3}, {"4", KEY_4},
        {"5", KEY_5}, {"6", KEY_6}, {"7", KEY_7}, {"8", KEY_8}, {"9", KEY_9},
        // Navigation
        {"up", KEY_UP}, {"down", KEY_DOWN}, {"left", KEY_LEFT}, {"right", KEY_RIGHT},
        {"space", KEY_SPACE}, {"enter", KEY_ENTER}, {"tab", KEY_TAB},
        {"esc", KEY_ESC}, {"backspace", KEY_BACKSPACE},
        {"home", KEY_HOME}, {"end", KEY_END},
        {"pageup", KEY_PAGEUP}, {"pagedown", KEY_PAGEDOWN},
        // Function keys
        {"f1", KEY_F1}, {"f2", KEY_F2}, {"f3", KEY_F3}, {"f4", KEY_F4},
        {"f5", KEY_F5}, {"f6", KEY_F6}, {"f7", KEY_F7}, {"f8", KEY_F8},
        {"f9", KEY_F9}, {"f10", KEY_F10}, {"f11", KEY_F11}, {"f12", KEY_F12},
        // Punctuation
        {"minus", KEY_MINUS}, {"equal", KEY_EQUAL},
        {"comma", KEY_COMMA}, {"dot", KEY_DOT}, {"slash", KEY_SLASH},
    };
    return map;
}

uint16_t key_name_to_code(const std::string& name) {
    const auto& map = get_key_map();
    auto it = map.find(name);
    return (it != map.end()) ? it->second : 0;
}

std::string key_code_to_name(uint16_t code) {
    const auto& map = get_key_map();
    for (const auto& [name, c] : map) {
        if (c == code) return name;
    }
    return "unknown";
}

bool is_valid_key_name(const std::string& name) {
    return get_key_map().count(name) > 0;
}

// --- ConfigModule ---

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

    // Global keys
    nlohmann::json gk;
    if (config.global_keys.hide_videos) {
        gk["hide_videos"] = key_code_to_name(*config.global_keys.hide_videos);
    }
    j["global_keys"] = gk;

    // Stock keys
    nlohmann::json sk;
    if (config.stock_keys.next_stock) sk["next_stock"] = key_code_to_name(*config.stock_keys.next_stock);
    if (config.stock_keys.prev_stock) sk["prev_stock"] = key_code_to_name(*config.stock_keys.prev_stock);
    if (config.stock_keys.next_chart) sk["next_chart"] = key_code_to_name(*config.stock_keys.next_chart);
    if (config.stock_keys.prev_chart) sk["prev_chart"] = key_code_to_name(*config.stock_keys.prev_chart);
    if (!sk.empty()) j["stock_keys"] = sk;

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
        vj["start_trigger"] = v.start_trigger_name;

        nlohmann::json keys_j;
        if (v.keys.next) keys_j["next"] = key_code_to_name(*v.keys.next);
        if (v.keys.prev) keys_j["prev"] = key_code_to_name(*v.keys.prev);
        if (v.keys.skip_forward) keys_j["skip_forward"] = key_code_to_name(*v.keys.skip_forward);
        if (v.keys.skip_backward) keys_j["skip_backward"] = key_code_to_name(*v.keys.skip_backward);
        if (!keys_j.empty()) vj["keys"] = keys_j;

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
        config.location.address = "Hasenbuk, Nürnberg, Germany";
        
        auto coords = geocode_address(config.location.address);
        if (coords) {
            config.location.lat = coords.value().first;
            config.location.lon = coords.value().second;
        } else {
            config.location.lat = 49.4521f;
            config.location.lon = 11.0767f;
        }

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

        VideoConfig v;
        v.enabled = true;
        v.audio_enabled = false;
        v.audio_device = "default";
        v.playlists = {"tests/sample.mp4"};
        v.x = 0.70f; v.y = 0.03f; v.w = 0.25f; v.h = 0.20f;
        v.src_x = 0.0f; v.src_y = 0.0f; v.src_w = 1.0f; v.src_h = 1.0f;
        config.videos.push_back(v);

        // Default global key: 'v' to hide/show videos
        config.global_keys.hide_videos = key_name_to_code("v");

        needs_save = true;
    } else {
        try {
            nlohmann::json j;
            in >> j;

            if (j.contains("location")) {
                config.location.address = j["location"].value("address", "Hasenbuk, Nürnberg, Germany");
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
                needs_save = true;
            }

            // Parse global_keys
            if (j.contains("global_keys")) {
                auto& gk = j["global_keys"];
                if (gk.contains("hide_videos") && gk["hide_videos"].is_string()) {
                    std::string key_name = gk["hide_videos"];
                    uint16_t code = key_name_to_code(key_name);
                    if (code > 0) {
                        config.global_keys.hide_videos = code;
                    } else {
                        std::cerr << "[Config] Warning: Unknown key name '" << key_name << "' for hide_videos.\n";
                    }
                }
            }

            // Parse stock_keys
            if (j.contains("stock_keys") && j["stock_keys"].is_object()) {
                auto& sk = j["stock_keys"];
                auto parse_sk = [&](const std::string& field) -> std::optional<uint16_t> {
                    if (sk.contains(field) && sk[field].is_string()) {
                        std::string name = sk[field];
                        uint16_t code = key_name_to_code(name);
                        if (code > 0) return code;
                        std::cerr << "[Config] Warning: Unknown key name '" << name << "' for stock_keys." << field << ".\n";
                    }
                    return std::nullopt;
                };
                config.stock_keys.next_stock = parse_sk("next_stock");
                config.stock_keys.prev_stock = parse_sk("prev_stock");
                config.stock_keys.next_chart = parse_sk("next_chart");
                config.stock_keys.prev_chart = parse_sk("prev_chart");
            }

            // Parse video key helper
            auto parse_optional_key = [](const nlohmann::json& parent, const std::string& field) -> std::optional<uint16_t> {
                if (parent.contains(field) && parent[field].is_string()) {
                    std::string name = parent[field];
                    uint16_t code = key_name_to_code(name);
                    if (code > 0) return code;
                    std::cerr << "[Config] Warning: Unknown key name '" << name << "' for " << field << ".\n";
                }
                return std::nullopt;
            };
            
            auto parse_video = [&parse_optional_key](const nlohmann::json& video_json) {
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

                // Start trigger
                v.start_trigger_name = video_json.value("start_trigger", "auto");
                if (v.start_trigger_name != "auto") {
                    uint16_t code = key_name_to_code(v.start_trigger_name);
                    if (code > 0) {
                        v.start_trigger_key = code;
                    } else {
                        std::cerr << "[Config] Warning: Unknown start_trigger key '" << v.start_trigger_name << "'. Defaulting to auto.\n";
                        v.start_trigger_name = "auto";
                        v.start_trigger_key = 0;
                    }
                }

                // Per-playlist navigation keys
                if (video_json.contains("keys") && video_json["keys"].is_object()) {
                    auto& keys_json = video_json["keys"];
                    v.keys.next = parse_optional_key(keys_json, "next");
                    v.keys.prev = parse_optional_key(keys_json, "prev");
                    v.keys.skip_forward = parse_optional_key(keys_json, "skip_forward");
                    v.keys.skip_backward = parse_optional_key(keys_json, "skip_backward");
                }

                return v;
            };

            if (j.contains("videos") && j["videos"].is_array()) {
                for (const auto& v_json : j["videos"]) {
                    config.videos.push_back(parse_video(v_json));
                }
            } else if (j.contains("video")) {
                config.videos.push_back(parse_video(j["video"]));
            } else {
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
