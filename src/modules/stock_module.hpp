#pragma once

#include <string>
#include <vector>
#include <expected>
#include <map>
#include <mutex>
#include <atomic>

namespace nuc_display::core { class Renderer; }
namespace nuc_display::modules { class TextRenderer; }

namespace nuc_display::modules {

enum class StockError {
    NetworkError,
    ParseError
};

struct StockChart {
    std::string label; // "1D", "5D", "1M", "1Y"
    float change_percent;
    std::vector<float> prices; // Resampled to e.g. 100 points
};

struct StockData {
    std::string symbol;
    std::string name;
    std::string currency_symbol;
    float current_price;
    std::vector<StockChart> charts;
};

struct StockConfig {
    std::string symbol;
    std::string name;
    std::string currency_symbol;
};

class StockModule {
public:
    StockModule();
    ~StockModule();

    void add_symbol(const std::string& symbol, const std::string& name, const std::string& currency_symbol = "$");
    
    // Blocking fetch of all configured symbols (can be run in thread pool)
    void update_all_data();
    
    // Renders right-side stock dashboard with timed animations
    void render(core::Renderer& renderer, TextRenderer& text_renderer, double time_sec);

    // Manual navigation (key-driven)
    void next_stock();
    void prev_stock();
    void next_chart();
    void prev_chart();
    
    bool is_empty() const;

    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);

private:
    std::expected<StockData, StockError> fetch_stock(const StockConfig& config);

    std::vector<StockConfig> symbols_;
    std::vector<StockData> stock_data_;
    
    size_t current_index_ = 0;
    size_t current_chart_index_ = 0;
    double last_switch_time_ = 0.0;
    std::atomic<bool> manual_mode_{false};
    
    std::map<std::string, bool> icon_attempted_;
    std::map<std::string, uint32_t> icon_textures_;

    std::mutex mutex_;
};

} // namespace nuc_display::modules
