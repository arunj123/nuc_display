#pragma once

#include <string>
#include <vector>
#include <expected>

namespace nuc_display::core { class Renderer; }
namespace nuc_display::modules { class TextRenderer; }

namespace nuc_display::modules {

enum class StockError {
    NetworkError,
    ParseError
};

struct StockData {
    std::string symbol;
    std::string name;
    float current_price;
    float change_percent;
    std::vector<float> prices; // Historical points for sparkline
};

class StockModule {
public:
    StockModule();
    ~StockModule();

    void add_symbol(const std::string& symbol, const std::string& name);
    
    // Blocking fetch of all configured symbols (can be run in thread pool)
    void update_all_data();
    
    // Renders right-side stock dashboard with timed animations
    void render(core::Renderer& renderer, TextRenderer& text_renderer, double time_sec);

private:
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
    std::expected<StockData, StockError> fetch_stock(const std::string& symbol, const std::string& name);

    std::vector<std::pair<std::string, std::string>> symbols_;
    std::vector<StockData> stock_data_;
    
    size_t current_index_ = 0;
    double last_switch_time_ = 0.0;
};

} // namespace nuc_display::modules
