#include "stock_module.hpp"
#include "core/renderer.hpp"
#include "modules/text_renderer.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>

namespace nuc_display::modules {

StockModule::StockModule() {
    curl_global_init(CURL_GLOBAL_ALL);
}

StockModule::~StockModule() {
    curl_global_cleanup();
}

void StockModule::add_symbol(const std::string& symbol, const std::string& name) {
    symbols_.push_back({symbol, name});
}

size_t StockModule::WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::expected<StockData, StockError> StockModule::fetch_stock(const std::string& symbol, const std::string& name) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if (!curl) return std::unexpected(StockError::NetworkError);

    std::string url = "https://query1.finance.yahoo.com/v8/finance/chart/" + symbol + "?range=1d&interval=5m";

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "CURL Error Fetching Stock " << symbol << ": " << curl_easy_strerror(res) << "\n";
        return std::unexpected(StockError::NetworkError);
    }

    try {
        auto json = nlohmann::json::parse(readBuffer);
        auto result = json["chart"]["result"][0];
        
        float current_price = result["meta"]["regularMarketPrice"];
        float prev_close_price = result["meta"]["chartPreviousClose"];
        float change_percent = ((current_price - prev_close_price) / prev_close_price) * 100.0f;

        std::vector<float> prices;
        auto close_array = result["indicators"]["quote"][0]["close"];
        for (const auto& price : close_array) {
            if (!price.is_null()) {
                prices.push_back(price);
            }
        }

        return StockData{symbol, name, current_price, change_percent, prices};
    } catch (const std::exception& e) {
        std::cerr << "JSON Parse Error for Stock " << symbol << ": " << e.what() << "\n";
        return std::unexpected(StockError::ParseError);
    }
}

void StockModule::update_all_data() {
    std::vector<StockData> new_data;
    for (const auto& sym : symbols_) {
        auto res = fetch_stock(sym.first, sym.second);
        if (res) {
            new_data.push_back(res.value());
        }
    }
    // thread safe assignment
    stock_data_ = new_data;
}

void StockModule::render(core::Renderer& renderer, TextRenderer& text_renderer, double time_sec) {
    if (stock_data_.empty()) return;

    // Switch every 10 seconds
    if (time_sec - last_switch_time_ > 10.0) {
        current_index_ = (current_index_ + 1) % stock_data_.size();
        last_switch_time_ = time_sec;
    }

    const auto& data = stock_data_[current_index_];

    // Compute simple slide-up animation when switching
    float anim_progress = std::min(1.0, (time_sec - last_switch_time_) * 1.5); // 0.66s duration
    // ease out cubic
    float ease = 1.0f - std::pow(1.0f - anim_progress, 3.0f);
    
    // Y offset for animation
    float y_offset = (1.0f - ease) * 0.1f;

    // Opacity
    float alpha = ease;

    // We render on the right half (x around 0.55 to 0.95)
    float base_x = 0.55f;
    float current_y = 0.15f + y_offset;

    // 1. Draw Symbol Name
    text_renderer.set_pixel_size(0, 80);
    if (auto glyphs = text_renderer.shape_text(data.name)) {
        renderer.draw_text(glyphs.value(), base_x, current_y, 1.0f, 1.0f, 1.0f, 1.0f, alpha);
    }
    
    // Ticker Symbol
    text_renderer.set_pixel_size(0, 36);
    if (auto glyphs = text_renderer.shape_text(data.symbol)) {
        renderer.draw_text(glyphs.value(), base_x, current_y + 0.05f, 1.0f, 0.6f, 0.6f, 0.6f, alpha);
    }

    current_y += 0.20f;

    // 2. Draw Price
    std::stringstream price_ss;
    price_ss << std::fixed << std::setprecision(2) << "$" << data.current_price;
    text_renderer.set_pixel_size(0, 120);
    if (auto glyphs = text_renderer.shape_text(price_ss.str())) {
        renderer.draw_text(glyphs.value(), base_x, current_y, 1.0f, 1.0f, 1.0f, 1.0f, alpha);
    }

    // 3. Draw Change % (Green/Red)
    std::stringstream change_ss;
    change_ss << std::fixed << std::setprecision(2) << (data.change_percent >= 0 ? "+" : "") << data.change_percent << "%";
    text_renderer.set_pixel_size(0, 60);
    
    float r = data.change_percent >= 0 ? 0.2f : 1.0f;
    float g = data.change_percent >= 0 ? 0.8f : 0.3f;
    float b = 0.3f; // nice red/green
    
    if (auto glyphs = text_renderer.shape_text(change_ss.str())) {
        renderer.draw_text(glyphs.value(), base_x, current_y + 0.08f, 1.0f, r, g, b, alpha);
    }

    current_y += 0.15f;

    // 4. Draw Sparkline
    if (data.prices.size() > 2) {
        float chart_w = 0.35f;
        float chart_h = 0.25f;
        
        // Find min/max for scaling
        float min_p = data.prices[0], max_p = data.prices[0];
        for (float p : data.prices) {
            if (p < min_p) min_p = p;
            if (p > max_p) max_p = p;
        }

        // Slight padding
        float pad = (max_p - min_p) * 0.1f;
        if (pad < 0.01f) pad = 1.0f; // avoid div/0
        min_p -= pad; max_p += pad;

        std::vector<float> points;
        points.reserve(data.prices.size() * 2);

        for (size_t i = 0; i < data.prices.size(); i++) {
            float px = base_x + ((float)i / (data.prices.size() - 1)) * chart_w;
            float py = current_y + chart_h - ((data.prices[i] - min_p) / (max_p - min_p)) * chart_h;
            points.push_back(px);
            points.push_back(py);
        }

        // Draw line with green/red tint representing day trend
        renderer.draw_line_strip(points, r, g, b, alpha, 4.0f);
    }
}

} // namespace nuc_display::modules
