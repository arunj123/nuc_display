#pragma once

#include <string>
#include <vector>
#include <mutex>

#include "core/renderer.hpp"

namespace nuc_display::modules { class TextRenderer; }

namespace nuc_display::modules {

struct NewsItem {
    std::string title;
    std::string source;
};

class NewsModule {
public:
    NewsModule();
    ~NewsModule();

    // Blocking fetch (run in thread pool)
    void update_headlines();
    
    // Render scrolling headlines in specified region
    void render(core::Renderer& renderer, TextRenderer& text_renderer, 
                float x, float y, float w, float h, double time_sec);

    bool is_empty() const;

private:
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
    
    std::vector<NewsItem> headlines_;
    mutable std::mutex mutex_;
    
    // Performance optimizations: Cache for shaped headlines
    struct CachedHeadline {
        int index = -1;
        std::vector<std::vector<GlyphData>> lines;
        float block_h = 0.0f;
    } cache_;
};

} // namespace nuc_display::modules
