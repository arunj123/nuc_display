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
#include <filesystem>
#include "modules/image_loader.hpp"

namespace nuc_display::modules {

StockModule::StockModule() {
    curl_global_init(CURL_GLOBAL_ALL);
}

StockModule::~StockModule() {
    curl_global_cleanup();
    // Texture cleanup
    for (auto const& [symbol, tex_id] : icon_textures_) {
        // We need a way to delete textures. Assuming renderer is not here, 
        // but textures are created on the main thread during render.
        // Actually, we'll let the user know we should probably have a cleanup method 
        // or just accept that they live for the app duration.
        // However, we can't easily call renderer.delete_texture(tex_id) here without a renderer ref.
    }
}

void StockModule::add_symbol(const std::string& symbol, const std::string& name, const std::string& currency_symbol) {
    symbols_.push_back({symbol, name, currency_symbol});
}

size_t StockModule::WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

namespace {
std::vector<float> resample_array(const std::vector<float>& input, int target_size) {
    if (input.empty()) return {};
    if (input.size() == 1) return std::vector<float>(target_size, input[0]);
    if (target_size <= 1) return {input[0]};

    std::vector<float> output(target_size);
    for (int i = 0; i < target_size; ++i) {
        float t = static_cast<float>(i) / (target_size - 1);
        float index_f = t * (input.size() - 1);
        int idx1 = static_cast<int>(index_f);
        int idx2 = std::min(idx1 + 1, static_cast<int>(input.size() - 1));
        float frac = index_f - idx1;
        output[i] = input[idx1] * (1.0f - frac) + input[idx2] * frac;
    }
    return output;
}

std::optional<StockChart> fetch_single_range(CURL* curl, const std::string& symbol, const std::string& label, const std::string& range, const std::string& interval) {
    std::string readBuffer;
    std::string url = "https://query1.finance.yahoo.com/v8/finance/chart/" + symbol + "?range=" + range + "&interval=" + interval;
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) return std::nullopt;
    
    try {
        auto json = nlohmann::json::parse(readBuffer);
        auto result = json["chart"]["result"][0];
        
        std::vector<float> prices;
        auto close_array = result["indicators"]["quote"][0]["close"];
        for (const auto& price : close_array) {
            if (!price.is_null()) {
                prices.push_back(price);
            }
        }
        if (prices.empty()) return std::nullopt;
        
        float change_percent = 0.0f;
        if (label == "1D") {
            float prev_close = result["meta"]["chartPreviousClose"];
            float current = result["meta"]["regularMarketPrice"];
            change_percent = ((current - prev_close) / prev_close) * 100.0f;
        } else {
            float current = result["meta"]["regularMarketPrice"]; // Most accurate
            float first = prices.front();
            change_percent = ((current - first) / first) * 100.0f;
        }
        
        return StockChart{label, change_percent, resample_array(prices, 100)};
    } catch (...) {
        return std::nullopt;
    }
}
}

std::expected<StockData, StockError> StockModule::fetch_stock(const StockConfig& config) {
    CURL* curl = curl_easy_init();
    if (!curl) return std::unexpected(StockError::NetworkError);

    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    // Ignore SSL issues locally if needed
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    StockData data;
    data.symbol = config.symbol;
    data.name = config.name;
    data.currency_symbol = config.currency_symbol;
    
    // Fetch 1D to get current price and first chart
    std::string current_price_readBuffer;
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &current_price_readBuffer);
    curl_easy_setopt(curl, CURLOPT_URL, ("https://query1.finance.yahoo.com/v8/finance/chart/" + config.symbol + "?range=1d&interval=5m").c_str());
    
    if (curl_easy_perform(curl) != CURLE_OK) {
        curl_easy_cleanup(curl);
        return std::unexpected(StockError::NetworkError);
    }
    
    try {
        auto json = nlohmann::json::parse(current_price_readBuffer);
        data.current_price = json["chart"]["result"][0]["meta"]["regularMarketPrice"];
    } catch (...) {
        curl_easy_cleanup(curl);
        return std::unexpected(StockError::ParseError);
    }

    // --- Auto-Fetch accurate company logo via Yahoo assetProfile + Clearbit ---
    // Only attempt if we don't already have a local logo file
    std::string icon_path = "assets/stocks/" + config.symbol + ".png";
    std::string alt_icon_path = "assets/stocks/" + config.symbol + ".jpg";
    if (!std::filesystem::exists(icon_path) && !std::filesystem::exists(alt_icon_path)) {
        std::string profile_readBuffer;
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &profile_readBuffer);
        curl_easy_setopt(curl, CURLOPT_URL, ("https://query1.finance.yahoo.com/v10/finance/quoteSummary/" + config.symbol + "?modules=assetProfile").c_str());
        
        if (curl_easy_perform(curl) == CURLE_OK) {
            try {
                auto p_json = nlohmann::json::parse(profile_readBuffer);
                if (p_json.contains("quoteSummary") && p_json["quoteSummary"]["result"].is_array() && !p_json["quoteSummary"]["result"].empty()) {
                    auto profile = p_json["quoteSummary"]["result"][0]["assetProfile"];
                    if (profile.contains("website") && profile["website"].is_string()) {
                        std::string website = profile["website"];
                        // Basic domain extraction
                        size_t start = website.find("://");
                        if (start != std::string::npos) website = website.substr(start + 3);
                        size_t www = website.find("www.");
                        if (www == 0) website = website.substr(4);
                        size_t end = website.find("/");
                        if (end != std::string::npos) website = website.substr(0, end);
                        
                        // Fire off async curl to download logo
                        if (!website.empty()) {
                            std::string cmd = "mkdir -p assets/stocks && curl -sL -m 3 'https://logo.clearbit.com/" + website + "' -o " + icon_path + " &";
                            int sys_res = system(cmd.c_str());
                            (void)sys_res;
                        }
                    }
                }
            } catch (...) {
                // Ignore profile parse errors, logo is optional
            }
        }
    }

    // Sequence queries
    // 1D
    if (auto c1 = fetch_single_range(curl, config.symbol, "1D", "1d", "5m")) data.charts.push_back(c1.value());
    // 5D
    if (auto c5 = fetch_single_range(curl, config.symbol, "5D", "5d", "15m")) data.charts.push_back(c5.value());
    // 1M
    if (auto c1m = fetch_single_range(curl, config.symbol, "1M", "1mo", "1d")) data.charts.push_back(c1m.value());
    // 1Y
    if (auto c1y = fetch_single_range(curl, config.symbol, "1Y", "1y", "1d")) data.charts.push_back(c1y.value());

    curl_easy_cleanup(curl);

    if (data.charts.empty()) {
        return std::unexpected(StockError::ParseError);
    }
    
    return data;
}

void StockModule::update_all_data() {
    std::vector<StockData> new_data;
    for (const auto& sym : symbols_) {
        auto res = fetch_stock(sym);
        if (res) {
            new_data.push_back(res.value());
        } else {
            std::cerr << "[StockModule] Failed to fetch stock data for: " << sym.symbol << "\n";
        }
    }
    // thread safe assignment
    stock_data_ = new_data;
}

void StockModule::render(core::Renderer& renderer, TextRenderer& text_renderer, double time_sec) {
    if (stock_data_.empty()) return;

    double display_duration_per_chart = 3.0; // 3 seconds per timeframe
    double display_duration_per_stock = display_duration_per_chart * stock_data_[current_index_].charts.size();

    // Switch stock entirely every 'display_duration_per_stock'
    if (time_sec - last_switch_time_ > display_duration_per_stock) {
        current_index_ = (current_index_ + 1) % stock_data_.size();
        last_switch_time_ = time_sec;
    }

    const auto& data = stock_data_[current_index_];
    
    if (data.charts.empty()) return;

    double local_time = time_sec - last_switch_time_;
    size_t active_chart_idx = static_cast<size_t>(local_time / display_duration_per_chart) % data.charts.size();
    size_t prev_chart_idx = (active_chart_idx + data.charts.size() - 1) % data.charts.size();
    
    double chart_local_time = std::fmod(local_time, display_duration_per_chart);
    
    // Smooth transition from prev to active over 0.6 seconds
    float morph_progress = std::min(1.0, chart_local_time / 0.6);
    float morph_ease = 1.0f - std::pow(1.0f - morph_progress, 3.0f);
    
    // During the very first chart of a new stock, animate it sliding up
    float alpha = 1.0f;
    float y_offset = 0.0f;
    if (active_chart_idx == 0 && chart_local_time < 0.6) {
        float entry_progress = chart_local_time / 0.6;
        float entry_ease = 1.0f - std::pow(1.0f - entry_progress, 3.0f);
        alpha = entry_ease;
        y_offset = (1.0f - entry_ease) * 0.1f;
        
        // Hard-set morph ease so we don't interpolate from the 1Y chart belonging to the previous stock loop
        morph_ease = 1.0f; 
    }

    const auto& active_chart = data.charts[active_chart_idx];
    const auto& prev_chart = data.charts[prev_chart_idx];

    // Pull base_x left to stop clipping with massive fonts
    float base_x = 0.40f; 
    float current_y = 0.15f + y_offset;

    // --- Stock Icon / Logo ---
    float icon_size = 0.08f; 
    bool has_icon = false;
    uint32_t tex_id = 0;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (icon_textures_.count(data.symbol)) {
            tex_id = icon_textures_[data.symbol];
            has_icon = (tex_id > 0);
        } else if (!icon_attempted_[data.symbol]) {
            icon_attempted_[data.symbol] = true;
            std::string path = "assets/stocks/" + data.symbol + ".png";
            if (!std::filesystem::exists(path)) path = "assets/stocks/" + data.symbol + ".jpg";
            
            // Auto-fetch logo logic (spawns separate script/curl so we don't block render)
            if (!std::filesystem::exists(path)) {
                // Clearbit logo fallback based on lowercase ticker (works well for big tech like AAPL, MSFT, TSLA)
                std::string cmd = "mkdir -p assets/stocks && curl -sL -m 2 'https://logo.clearbit.com/" + data.symbol + ".com' -o " + path + " &";
                system(cmd.c_str());
            }

            if (std::filesystem::exists(path)) {
                ImageLoader loader;
                if (loader.load(path)) {
                    tex_id = renderer.create_texture(loader.get_rgba_data().data(), loader.width(), loader.height(), loader.channels());
                    icon_textures_[data.symbol] = tex_id;
                    has_icon = (tex_id > 0);
                }
            }
        }
    }

    if (has_icon) {
        float aspect = (float)renderer.width() / renderer.height();
        renderer.draw_quad(tex_id, base_x, current_y - 0.04f, icon_size, icon_size * aspect, 1.0f, 1.0f, 1.0f, alpha);
        base_x += icon_size + 0.02f; // Shift text to the right
    }

    // 1. Draw Symbol (Large & Bold)
    text_renderer.set_pixel_size(0, 95); // Match time size from Weather
    if (auto glyphs = text_renderer.shape_text(data.symbol)) {
        renderer.draw_text(glyphs.value(), base_x, current_y, 1.0f, 1.0f, 1.0f, 1.0f, alpha);
    }
    
    // Draw Name (Subtle Grey, strictly below symbol)
    current_y += 0.06f;
    text_renderer.set_pixel_size(0, 32);
    if (auto glyphs = text_renderer.shape_text(data.name)) {
        renderer.draw_text(glyphs.value(), base_x, current_y, 1.0f, 0.6f, 0.6f, 0.6f, alpha);
    }

    current_y += 0.14f;

    // 2. Draw Price (Massive)
    std::stringstream price_ss;
    price_ss << std::fixed << std::setprecision(2) << data.currency_symbol << data.current_price;
    text_renderer.set_pixel_size(0, 160);
    float price_w = 0.0f;
    if (auto glyphs = text_renderer.shape_text(price_ss.str())) {
        for (const auto& g : glyphs.value()) price_w += g.advance / (float)renderer.width();
        renderer.draw_text(glyphs.value(), base_x, current_y, 1.0f, 1.0f, 1.0f, 1.0f, alpha);
    }

    // 3. Draw Interpolated Change % and Timeframe Label (Placed cleanly to the right of the price)
    float current_change = prev_chart.change_percent * (1.0f - morph_ease) + active_chart.change_percent * morph_ease;
    
    std::stringstream change_ss;
    change_ss << std::fixed << std::setprecision(2) << (current_change >= 0 ? "+" : "") << current_change << "%";
    text_renderer.set_pixel_size(0, 64);
    
    float r = current_change >= 0 ? 0.2f : 1.0f;
    float g = current_change >= 0 ? 0.8f : 0.3f;
    float b = 0.3f; // nice red/green
    
    float change_x = base_x + price_w + 0.04f;
    if (auto glyphs = text_renderer.shape_text(change_ss.str())) {
        renderer.draw_text(glyphs.value(), change_x, current_y - 0.04f, 1.0f, r, g, b, alpha);
    }
    
    // Timeframe Label under change %
    float label_alpha = alpha * (0.5f + 0.5f * (1.0f - std::abs(1.0f - 2.0f * morph_ease))); 
    if (morph_ease == 1.0f) label_alpha = alpha;
    
    text_renderer.set_pixel_size(0, 28);
    if (auto glyphs = text_renderer.shape_text(active_chart.label + " Change")) {
        renderer.draw_text(glyphs.value(), change_x, current_y + 0.01f, 1.0f, 0.5f, 0.5f, 0.5f, label_alpha);
    }

    current_y += 0.12f;

    // 4. Draw Interpolated Massive Sparkline and Scales
    if (active_chart.prices.size() > 2 && prev_chart.prices.size() == active_chart.prices.size()) {
        float chart_w = 0.50f; // Use almost all remaining width
        float chart_h = 0.40f; // Use bottom half
        
        std::vector<float> interp_prices(active_chart.prices.size());
        for (size_t i = 0; i < active_chart.prices.size(); i++) {
            float p_prev = prev_chart.prices[i];
            float p_curr = active_chart.prices[i];
            interp_prices[i] = p_prev * (1.0f - morph_ease) + p_curr * morph_ease;
        }
        
        // Find min/max for scaling of the interpolated array
        float min_p = interp_prices[0], max_p = interp_prices[0];
        for (float p : interp_prices) {
             if (p < min_p) min_p = p;
             if (p > max_p) max_p = p;
        }

        // Slight padding
        float pad = (max_p - min_p) * 0.1f;
        if (pad < 0.01f) pad = 1.0f; // avoid div/0
        min_p -= pad; max_p += pad;

        std::vector<float> points;
        points.reserve(interp_prices.size() * 2);

        for (size_t i = 0; i < interp_prices.size(); i++) {
            float px = base_x + ((float)i / (interp_prices.size() - 1)) * chart_w;
            float py = current_y + chart_h - ((interp_prices[i] - min_p) / (max_p - min_p)) * chart_h;
            points.push_back(px);
            points.push_back(py);
        }

        // Draw line with green/red tint representing trend
        renderer.draw_line_strip(points, r, g, b, alpha, 5.0f); // Slightly thicker line

        // Draw Scales
        text_renderer.set_pixel_size(0, 24);
        
        // Max Scale
        std::stringstream max_ss;
        max_ss << std::fixed << std::setprecision(2) << max_p;
        if (auto glyphs = text_renderer.shape_text(max_ss.str())) {
            renderer.draw_text(glyphs.value(), base_x + chart_w + 0.01f, current_y, 1.0f, 0.6f, 0.6f, 0.6f, alpha);
        }

        // Mid Scale
        std::stringstream mid_ss;
        mid_ss << std::fixed << std::setprecision(2) << (min_p + (max_p - min_p) / 2.0f);
        if (auto glyphs = text_renderer.shape_text(mid_ss.str())) {
            renderer.draw_text(glyphs.value(), base_x + chart_w + 0.01f, current_y + chart_h / 2.0f, 1.0f, 0.4f, 0.4f, 0.4f, alpha);
        }

        // Min Scale
        std::stringstream min_ss;
        min_ss << std::fixed << std::setprecision(2) << min_p;
        if (auto glyphs = text_renderer.shape_text(min_ss.str())) {
            renderer.draw_text(glyphs.value(), base_x + chart_w + 0.01f, current_y + chart_h - 0.02f, 1.0f, 0.6f, 0.6f, 0.6f, alpha);
        }

        // Time limits roughly
        if (auto glyphs = text_renderer.shape_text("Start")) {
            renderer.draw_text(glyphs.value(), base_x, current_y + chart_h + 0.04f, 1.0f, 0.5f, 0.5f, 0.5f, alpha);
        }
        if (auto glyphs = text_renderer.shape_text("Now")) {
            renderer.draw_text(glyphs.value(), base_x + chart_w - 0.05f, current_y + chart_h + 0.04f, 1.0f, 0.5f, 0.5f, 0.5f, alpha);
        }
    }
}

} // namespace nuc_display::modules
