#include "news_module.hpp"
#include "text_renderer.hpp"
#include "../core/renderer.hpp"

#include <curl/curl.h>
#include <iostream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <cmath>

namespace nuc_display::modules {

NewsModule::NewsModule() {}
NewsModule::~NewsModule() {}

size_t NewsModule::WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

void NewsModule::update_headlines() {
    CURL* curl = curl_easy_init();
    if (!curl) return;

    std::string readBuffer;
    
    // Google News RSS - Top Stories (English)
    std::string url = "https://news.google.com/rss?hl=en&gl=US&ceid=US:en";
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "[NewsModule] CURL error: " << curl_easy_strerror(res) << "\n";
        return;
    }

    // Simple XML parsing for <title> and <source> tags within <item> blocks
    std::vector<NewsItem> items;
    
    // Find all <item>...</item> blocks
    std::string::size_type pos = 0;
    // Skip the first <title> which is the feed title
    bool first_title_skipped = false;
    
    while ((pos = readBuffer.find("<item>", pos)) != std::string::npos) {
        auto end_pos = readBuffer.find("</item>", pos);
        if (end_pos == std::string::npos) break;
        
        std::string item_block = readBuffer.substr(pos, end_pos - pos);
        
        NewsItem item;
        
        // Extract <title>
        auto title_start = item_block.find("<title>");
        auto title_end = item_block.find("</title>");
        if (title_start != std::string::npos && title_end != std::string::npos) {
            title_start += 7; // length of "<title>"
            item.title = item_block.substr(title_start, title_end - title_start);
            
            // Remove CDATA wrapper if present
            if (item.title.find("<![CDATA[") == 0) {
                item.title = item.title.substr(9, item.title.length() - 12);
            }
            
            // Remove HTML entities
            std::string clean;
            for (size_t i = 0; i < item.title.length(); i++) {
                if (item.title[i] == '&') {
                    auto semi = item.title.find(';', i);
                    if (semi != std::string::npos) {
                        std::string entity = item.title.substr(i, semi - i + 1);
                        if (entity == "&amp;") clean += '&';
                        else if (entity == "&lt;") clean += '<';
                        else if (entity == "&gt;") clean += '>';
                        else if (entity == "&apos;") clean += '\'';
                        else if (entity == "&quot;") clean += '"';
                        else if (entity == "&#39;") clean += '\'';
                        else clean += entity;
                        i = semi;
                        continue;
                    }
                }
                clean += item.title[i];
            }
            item.title = clean;
        }
        
        // Extract <source>
        auto src_start = item_block.find("<source");
        auto src_end = item_block.find("</source>");
        if (src_start != std::string::npos && src_end != std::string::npos) {
            auto tag_end = item_block.find(">", src_start);
            if (tag_end != std::string::npos && tag_end < src_end) {
                item.source = item_block.substr(tag_end + 1, src_end - tag_end - 1);
            }
        }
        
        if (!item.title.empty()) {
            items.push_back(std::move(item));
        }
        
        pos = end_pos + 7;
        
        if (items.size() >= 10) break; // Cap at 10 headlines
    }
    
    if (!items.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        headlines_ = std::move(items);
        std::cout << "[NewsModule] Fetched " << headlines_.size() << " headlines.\n";
    }
}

void NewsModule::render(core::Renderer& renderer, TextRenderer& text_renderer,
                        float x, float y, float w, float h, double time_sec) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (headlines_.empty()) return;

    // Section header
    text_renderer.set_pixel_size(0, 22);
    if (auto glyphs = text_renderer.shape_text("Headlines")) {
        renderer.draw_text(glyphs.value(), x, y, 1.0f, 0.5f, 0.5f, 0.5f, 1.0f);
    }
    
    // We will display just ONE headline at a time in the center of the news area.
    // Time-based state machine layout:
    // Cycle length: 12 seconds per headline
    // 0.0 - 1.0s: Slide up from bottom (fade in)
    // 1.0 - 3.0s: Pause
    // 3.0 - 11.0s: Horizontal scroll (if wider than w) or just Pause (if fits)
    // 11.0 - 12.0s: Slide up to top (fade out)
    
    const double cycle_duration = 12.0;
    int headline_idx = static_cast<int>(time_sec / cycle_duration) % headlines_.size();
    double phase_time = std::fmod(time_sec, cycle_duration);
    
    const auto& item = headlines_[headline_idx];
    std::string prefix = "- ";
    std::string full_text = prefix + item.title + " (" + item.source + ")";
    
    // Calculate total text width
    text_renderer.set_pixel_size(0, 24); // Slightly larger font for single-item display
    auto glyphs_opt = text_renderer.shape_text(full_text);
    if (!glyphs_opt) return;
    
    float text_width = 0.0f;
    for (const auto& g : glyphs_opt.value()) {
        text_width += g.advance / (float)renderer.width();
    }
    
    // Y-axis animation
    float center_y = y + h * 0.5f;
    float current_y = center_y;
    float alpha = 1.0f;
    
    if (phase_time < 1.0) {
        // Slide in
        float t = phase_time; // 0 to 1
        t = 1.0f - std::pow(1.0f - t, 3.0f); // ease out cubic
        current_y = center_y + (1.0f - t) * (h * 0.4f);
        alpha = t;
    } else if (phase_time > 11.0) {
        // Slide out
        float t = phase_time - 11.0; // 0 to 1
        t = t * t * t; // ease in cubic
        current_y = center_y - t * (h * 0.4f);
        alpha = 1.0f - t;
    }
    
    // X-axis animation (marquee)
    float current_x = x;
    if (text_width > w) {
        if (phase_time > 3.0 && phase_time <= 11.0) {
            float scroll_progress = (phase_time - 3.0) / 8.0; // 0 to 1
            // ease in out for marquee
            scroll_progress = scroll_progress * scroll_progress * (3.0f - 2.0f * scroll_progress); 
            float max_scroll = text_width - w + 0.05f; // scroll past a bit
            current_x = x - (scroll_progress * max_scroll);
        } else if (phase_time > 11.0) {
            // Keep it fully scrolled during slide out
            current_x = x - (text_width - w + 0.05f);
        }
    }
    
    // Enable scissor cut-off for the text so it doesn't overflow left/right column bounds
    glEnable(GL_SCISSOR_TEST);
    // Scissor expects x, y, w, h in viewport coordinates (bottom-left origin)
    int vp_x = static_cast<int>(x * renderer.width());
    // Renderer Y is top-down (0 = top), GL Scissor Y is bottom-up (0 = bottom)
    int vp_y = static_cast<int>((1.0f - (y + h)) * renderer.height());
    int vp_w = static_cast<int>(w * renderer.width());
    int vp_h = static_cast<int>(h * renderer.height());
    glScissor(vp_x, vp_y, vp_w, vp_h);

    renderer.draw_text(glyphs_opt.value(), current_x, current_y, 1.0f, 0.8f, 0.8f, 0.8f, alpha);
    
    glDisable(GL_SCISSOR_TEST);
}

} // namespace nuc_display::modules
