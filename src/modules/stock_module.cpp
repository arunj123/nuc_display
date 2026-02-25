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
#include <fstream>
#include "modules/image_loader.hpp"

namespace nuc_display::modules {

StockModule::StockModule() {}

StockModule::~StockModule() {
    // Textures live for the app duration — no renderer ref available here
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
    if (!curl) return std::nullopt;
    std::string readBuffer;
    std::string url = "https://query1.finance.yahoo.com/v8/finance/chart/" + symbol + "?range=" + range + "&interval=" + interval;
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StockModule::WriteCallback);
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
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    StockData data;
    data.symbol = config.symbol;
    data.name = config.name;
    data.currency_symbol = config.currency_symbol;
    
    // Fetch 1D to get current price and first chart
    std::string current_price_readBuffer;
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &current_price_readBuffer);
    curl_easy_setopt(curl, CURLOPT_URL, ("https://query1.finance.yahoo.com/v8/finance/chart/" + config.symbol + "?range=1d&interval=5m").c_str());
    
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "[StockModule] CURL error for " << config.symbol << " (current price): " << curl_easy_strerror(res) << "\n";
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

    // --- Auto-Fetch company logo via Google Favicon API ---
    std::string icon_path = "assets/stocks/" + config.symbol + ".png";
    if (!std::filesystem::exists(icon_path) || std::filesystem::file_size(icon_path) < 200) {
        // Symbol-to-domain lookup for configured stocks
        static const std::map<std::string, std::string> symbol_domains = {
            {"^IXIC", "nasdaq.com"}, {"^GSPC", "spglobal.com"},
            {"^NSEI", "nseindia.com"}, {"^BSESN", "bseindia.com"},
            {"APC.F", "apple.com"}, {"MSF.F", "microsoft.com"},
            {"NVD.F", "nvidia.com"}, {"AMZ.F", "amazon.com"},
            {"FB2A.F", "meta.com"}, {"ABEA.F", "alphabet.com"},
            {"TL0.F", "tesla.com"},
            // US versions
            {"AAPL", "apple.com"}, {"MSFT", "microsoft.com"},
            {"NVDA", "nvidia.com"}, {"AMZN", "amazon.com"},
            {"META", "meta.com"}, {"GOOGL", "alphabet.com"},
            {"TSLA", "tesla.com"},
        };

        auto it = symbol_domains.find(config.symbol);
        if (it != symbol_domains.end()) {
            std::filesystem::create_directories("assets/stocks");

            std::string favicon_url = "https://www.google.com/s2/favicons?domain=" + it->second + "&sz=128";
            std::string logo_data;
            
            CURL* logo_curl = curl_easy_init();
            if (logo_curl) {
                curl_easy_setopt(logo_curl, CURLOPT_URL, favicon_url.c_str());
                curl_easy_setopt(logo_curl, CURLOPT_USERAGENT, "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36");
                curl_easy_setopt(logo_curl, CURLOPT_WRITEFUNCTION, WriteCallback);
                curl_easy_setopt(logo_curl, CURLOPT_WRITEDATA, &logo_data);
                curl_easy_setopt(logo_curl, CURLOPT_FOLLOWLOCATION, 1L);
                curl_easy_setopt(logo_curl, CURLOPT_TIMEOUT, 10L);
                curl_easy_setopt(logo_curl, CURLOPT_SSL_VERIFYPEER, 1L);

                CURLcode logo_res = curl_easy_perform(logo_curl);
                curl_easy_cleanup(logo_curl);

                if (logo_res == CURLE_OK && logo_data.size() > 200) {
                    std::ofstream out(icon_path, std::ios::binary);
                    if (out.is_open()) {
                        out.write(logo_data.data(), logo_data.size());
                        out.close();
                        std::cout << "[Stock] Downloaded logo for " << config.symbol << " from " << it->second << "\n";
                    }
                } else {
                    std::cerr << "[Stock] Logo fetch failed for " << config.symbol << " (" << it->second << ")\n";
                }
            }
        }
    }


    // Sequence queries — all timeframes
    struct RangeSpec { const char* label; const char* range; const char* interval; };
    constexpr RangeSpec ranges[] = {
        {"1D",  "1d",  "5m"},
        {"5D",  "5d",  "15m"},
        {"1M",  "1mo", "1d"},
        {"3M",  "3mo", "1wk"},
        {"6M",  "6mo", "1wk"},
        {"YTD", "ytd", "1d"},
        {"1Y",  "1y",  "1d"},
    };
    for (const auto& r : ranges) {
        if (auto c = fetch_single_range(curl, config.symbol, r.label, r.range, r.interval))
            data.charts.push_back(c.value());
    }

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
    std::lock_guard<std::mutex> lock(mutex_);
    stock_data_ = std::move(new_data);
}

void StockModule::next_stock() {
    if (stock_data_.empty()) return;
    manual_mode_ = true;
    current_index_ = (current_index_ + 1) % stock_data_.size();
    current_chart_index_ = 0;
    last_switch_time_ = -1.0; // Force reset on next render
    std::cout << "[Stock] Manual: next stock -> " << stock_data_[current_index_].symbol << "\n";
}

void StockModule::prev_stock() {
    if (stock_data_.empty()) return;
    manual_mode_ = true;
    current_index_ = (current_index_ + stock_data_.size() - 1) % stock_data_.size();
    current_chart_index_ = 0;
    last_switch_time_ = -1.0;
    std::cout << "[Stock] Manual: prev stock -> " << stock_data_[current_index_].symbol << "\n";
}

void StockModule::next_chart() {
    if (stock_data_.empty()) return;
    manual_mode_ = true;
    const auto& data = stock_data_[current_index_];
    if (!data.charts.empty()) {
        current_chart_index_ = (current_chart_index_ + 1) % data.charts.size();
    }
    last_switch_time_ = -1.0;
    std::cout << "[Stock] Manual: next chart -> " << (data.charts.empty() ? "N/A" : data.charts[current_chart_index_].label) << "\n";
}

void StockModule::prev_chart() {
    if (stock_data_.empty()) return;
    manual_mode_ = true;
    const auto& data = stock_data_[current_index_];
    if (!data.charts.empty()) {
        current_chart_index_ = (current_chart_index_ + data.charts.size() - 1) % data.charts.size();
    }
    last_switch_time_ = -1.0;
    std::cout << "[Stock] Manual: prev chart -> " << (data.charts.empty() ? "N/A" : data.charts[current_chart_index_].label) << "\n";
}

void StockModule::render(core::Renderer& renderer, TextRenderer& text_renderer, double time_sec) {
    if (stock_data_.empty()) return;

    double display_duration_per_chart = 3.0; // 3 seconds per timeframe
    size_t active_chart_idx = 0;

    if (manual_mode_) {
        // Manual mode: use stored indices, reset timer on first render after key press
        if (last_switch_time_ < 0.0) last_switch_time_ = time_sec;
        active_chart_idx = current_chart_index_;
    } else {
        double display_duration_per_stock = display_duration_per_chart * stock_data_[current_index_].charts.size();

        // Switch stock entirely every 'display_duration_per_stock'
        if (time_sec - last_switch_time_ > display_duration_per_stock) {
            current_index_ = (current_index_ + 1) % stock_data_.size();
            last_switch_time_ = time_sec;
        }
    }

    const auto& data = stock_data_[current_index_];
    
    if (data.charts.empty()) return;

    double local_time = time_sec - last_switch_time_;

    if (!manual_mode_) {
        active_chart_idx = static_cast<size_t>(local_time / display_duration_per_chart) % data.charts.size();
    }
    
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

    size_t prev_chart_idx = (active_chart_idx + data.charts.size() - 1) % data.charts.size();
    const auto& active_chart = data.charts[active_chart_idx];
    const auto& prev_chart = data.charts[prev_chart_idx];

    // Pull base_x to the left to prevent the text from crossing the right edge of the screen
    float base_x = 0.44f; 
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
        } else {
            // Attempt to load the file (which might be downloading asynchronously by fetch_stock)
            std::string path = "assets/stocks/" + data.symbol + ".png";
            
            if (std::filesystem::exists(path) && std::filesystem::file_size(path) > 0) {
                // If it failed to decode previously, don't spam it every frame
                if (!icon_attempted_[data.symbol]) {
                    ImageLoader loader;
                    if (loader.load(path)) {
                        tex_id = renderer.create_texture(loader.get_rgba_data().data(), loader.width(), loader.height(), loader.channels());
                        icon_textures_[data.symbol] = tex_id;
                        has_icon = (tex_id > 0);
                        std::cout << "[Stock] Successfully loaded logo for " << data.symbol << std::endl;
                    } else {
                        std::cerr << "[Stock] Failed to load STB image for " << data.symbol << " at " << path << std::endl;
                        
                        // Mark as attempted so we don't spam loader on an invalid/corrupt image file
                        icon_attempted_[data.symbol] = true;
                        
                        // If it's extremely small, it's likely a 404 HTML body from Clearbit or empty. Delete it.
                        if (std::filesystem::file_size(path) < 2048) {
                            std::cerr << "[Stock] File too small, deleting " << path << std::endl;
                            std::filesystem::remove(path);
                        }
                    }
                }
            }
        }
    }

    float title_x = base_x;
    if (has_icon) {
        float aspect = (float)renderer.width() / renderer.height();
        renderer.draw_quad(tex_id, base_x, current_y - 0.06f, icon_size, icon_size * aspect, 1.0f, 1.0f, 1.0f, alpha);
        title_x += icon_size + 0.02f; // Shift ONLY the title text to the right
    }

    // 1. Draw Symbol (Large & Bold)
    text_renderer.set_pixel_size(0, 85); // Made slightly smaller to fit nicely next to the logo
    if (auto glyphs = text_renderer.shape_text(data.symbol)) {
        renderer.draw_text(glyphs.value(), title_x, current_y, 1.0f, 1.0f, 1.0f, 1.0f, alpha);
    }
    
    // Draw Name (Subtle Grey, strictly below symbol)
    current_y += 0.06f;
    text_renderer.set_pixel_size(0, 32);
    if (auto glyphs = text_renderer.shape_text(data.name)) {
        renderer.draw_text(glyphs.value(), title_x, current_y, 1.0f, 0.6f, 0.6f, 0.6f, alpha);
    }

    current_y += 0.14f;

    // 2. Draw Price (Massive)
    char price_buf[32];
    snprintf(price_buf, sizeof(price_buf), "%s%.2f", data.currency_symbol.c_str(), data.current_price);
    text_renderer.set_pixel_size(0, 160);
    float price_w = 0.0f;
    if (auto glyphs = text_renderer.shape_text(price_buf)) {
        for (const auto& g : glyphs.value()) price_w += g.advance / (float)renderer.width();
        renderer.draw_text(glyphs.value(), base_x, current_y, 1.0f, 1.0f, 1.0f, 1.0f, alpha);
    }

    // 3. Draw Interpolated Change % and Timeframe Label (Placed cleanly to the right of the price)
    float current_change = prev_chart.change_percent * (1.0f - morph_ease) + active_chart.change_percent * morph_ease;
    
    char change_buf[32];
    snprintf(change_buf, sizeof(change_buf), "%s%.2f%%", current_change >= 0 ? "+" : "", current_change);
    text_renderer.set_pixel_size(0, 64);
    
    float r = current_change >= 0 ? 0.2f : 1.0f;
    float g = current_change >= 0 ? 0.8f : 0.3f;
    float b = 0.3f; // nice red/green
    
    float change_x = base_x + price_w + 0.04f;
    if (auto glyphs = text_renderer.shape_text(change_buf)) {
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
        float chart_w = 0.50f;
        float chart_h = 0.40f;

        // Stack-allocated arrays instead of heap vectors (max 200 data points)
        constexpr size_t MAX_CHART = 200;
        size_t n = std::min(active_chart.prices.size(), MAX_CHART);
        float interp_prices[MAX_CHART];
        for (size_t i = 0; i < n; i++) {
            interp_prices[i] = prev_chart.prices[i] * (1.0f - morph_ease) + active_chart.prices[i] * morph_ease;
        }
        
        float min_p = interp_prices[0], max_p = interp_prices[0];
        for (size_t i = 1; i < n; i++) {
             if (interp_prices[i] < min_p) min_p = interp_prices[i];
             if (interp_prices[i] > max_p) max_p = interp_prices[i];
        }

        float pad = (max_p - min_p) * 0.1f;
        if (pad < 0.01f) pad = 1.0f;
        min_p -= pad; max_p += pad;

        float points[MAX_CHART * 2];
        for (size_t i = 0; i < n; i++) {
            points[i * 2]     = base_x + ((float)i / (n - 1)) * chart_w;
            points[i * 2 + 1] = current_y + chart_h - ((interp_prices[i] - min_p) / (max_p - min_p)) * chart_h;
        }

        // Draw line with green/red trend
        renderer.draw_line_strip(points, n * 2, r, g, b, alpha, 5.0f);

        // Draw Scales
        text_renderer.set_pixel_size(0, 24);
        
        char max_buf[16]; snprintf(max_buf, sizeof(max_buf), "%.2f", max_p);
        if (auto glyphs = text_renderer.shape_text(max_buf)) {
            renderer.draw_text(glyphs.value(), base_x + chart_w + 0.01f, current_y, 1.0f, 0.6f, 0.6f, 0.6f, alpha);
        }

        char mid_buf[16]; snprintf(mid_buf, sizeof(mid_buf), "%.2f", min_p + (max_p - min_p) / 2.0f);
        if (auto glyphs = text_renderer.shape_text(mid_buf)) {
            renderer.draw_text(glyphs.value(), base_x + chart_w + 0.01f, current_y + chart_h / 2.0f, 1.0f, 0.4f, 0.4f, 0.4f, alpha);
        }

        char min_buf[16]; snprintf(min_buf, sizeof(min_buf), "%.2f", min_p);
        if (auto glyphs = text_renderer.shape_text(min_buf)) {
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

bool StockModule::is_empty() const {
    return stock_data_.empty();
}

} // namespace nuc_display::modules
