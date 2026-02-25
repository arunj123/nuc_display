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
    std::vector<std::string> urls = {
        "https://news.google.com/rss/search?q=stock+market&hl=en-US&gl=US&ceid=US:en",
        "http://feeds.bbci.co.uk/news/rss.xml"
    };

    std::vector<NewsItem> items;

    for (const auto& url : urls) {
        CURL* curl = curl_easy_init();
        if (!curl) continue;

        std::string readBuffer;
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            std::cerr << "[NewsModule] CURL error for " << url << ": " << curl_easy_strerror(res) << "\n";
            continue;
        }

        if (http_code != 200) {
            std::cerr << "[NewsModule] HTTP error " << http_code << " for " << url << "\n";
            continue;
        }

        if (readBuffer.empty()) {
            std::cerr << "[NewsModule] Received empty response from " << url << "\n";
            continue;
        }

        // Find all <item>...</item> blocks
        std::string::size_type pos = 0;
        while ((pos = readBuffer.find("<item>", pos)) != std::string::npos) {
            auto end_pos = readBuffer.find("</item>", pos);
            if (end_pos == std::string::npos) break;
            
            std::string item_block = readBuffer.substr(pos, end_pos - pos);
            NewsItem item;
            
            // Extract <title>
            auto title_start = item_block.find("<title>");
            auto title_end = item_block.find("</title>");
            if (title_start != std::string::npos && title_end != std::string::npos) {
                title_start += 7;
                std::string raw_title = item_block.substr(title_start, title_end - title_start);
                
                // Content can be wrapped in <![CDATA[ ]]>
                if (raw_title.find("<![CDATA[") == 0) {
                    auto cdata_end = raw_title.find("]]>");
                    if (cdata_end != std::string::npos) {
                        raw_title = raw_title.substr(9, cdata_end - 9);
                    }
                }
                
                // Basic HTML entity decoding
                std::string clean;
                for (size_t i = 0; i < raw_title.length(); i++) {
                    if (raw_title[i] == '&') {
                        auto semi = raw_title.find(';', i);
                        if (semi != std::string::npos) {
                            std::string entity = raw_title.substr(i, semi - i + 1);
                            if (entity == "&amp;") clean += '&';
                            else if (entity == "&lt;") clean += '<';
                            else if (entity == "&gt;") clean += '>';
                            else if (entity == "&apos;" || entity == "&#39;") clean += '\'';
                            else if (entity == "&quot;") clean += '"';
                            else clean += entity;
                            i = semi;
                            continue;
                        }
                    }
                    clean += raw_title[i];
                }
                item.title = clean;
            }
            
            // Extract <source> or use the domain from URL as fallback
            auto src_start = item_block.find("<source");
            auto src_end = item_block.find("</source>");
            if (src_start != std::string::npos && src_end != std::string::npos) {
                auto tag_end = item_block.find(">", src_start);
                if (tag_end != std::string::npos && tag_end < src_end) {
                    item.source = item_block.substr(tag_end + 1, src_end - tag_end - 1);
                }
            }
            if (item.source.empty()) {
                item.source = (url.find("google.com") != std::string::npos) ? "Google News" : "BBC News";
            }
            
            if (!item.title.empty()) {
                items.push_back(std::move(item));
            }
            pos = end_pos + 7;
            if (items.size() >= 10) break;
        }

        if (!items.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            headlines_ = std::move(items);
            std::cout << "[NewsModule] Successfully fetched " << headlines_.size() << " headlines from " << url << "\n";
            return; // Exit as soon as we have data
        }
    }
    
    std::cerr << "[NewsModule] Failed to fetch headlines from all sources.\n";
}

static std::vector<std::string> wrap_news_text(const std::string& text, size_t max_chars) {
    std::vector<std::string> lines;
    std::istringstream words(text);
    std::string word;
    std::string current_line;

    while (words >> word) {
        if (current_line.empty()) {
            current_line = word;
        } else if (current_line.length() + 1 + word.length() <= max_chars) {
            current_line += " " + word;
        } else {
            lines.push_back(current_line);
            current_line = word;
        }
    }
    if (!current_line.empty()) {
        lines.push_back(current_line);
    }
    return lines;
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
    
    const double cycle_duration = 12.0;
    int headline_idx = static_cast<int>(time_sec / cycle_duration) % headlines_.size();
    double phase_time = std::fmod(time_sec, cycle_duration);
    
    const auto& item = headlines_[headline_idx];
    
    float line_height = 0.035f;

    // Caching logic: If headline index changed, re-wrap and re-shape
    if (cache_.index != headline_idx) {
        std::string prefix = "- ";
        std::string full_text = prefix + item.title + " (" + item.source + ")";
        auto lines = wrap_news_text(full_text, 54);
        
        cache_.lines.clear();
        text_renderer.set_pixel_size(0, 24);
        for (const auto& line : lines) {
            if (auto glyphs_opt = text_renderer.shape_text(line)) {
                cache_.lines.push_back(std::move(glyphs_opt.value()));
            }
        }
        cache_.block_h = cache_.lines.size() * line_height;
        cache_.index = headline_idx;
    }
    
    float block_h = cache_.block_h;
    
    // Y-axis animation parameters
    float center_y_base = y + (h - block_h) * 0.5f + 0.02f; // +0.02f offset for header space
    float current_y = center_y_base;
    float alpha = 1.0f;
    
    // If block is extremely tall, vertically scroll it during the 3s - 11s phase
    if (block_h > h - 0.03f) {
        float max_scroll = block_h - (h - 0.05f); // scroll until bottom is visible
        if (phase_time > 2.0 && phase_time <= 11.0) {
            float scroll_progress = (phase_time - 2.0) / 9.0;
            current_y = y + 0.02f - (scroll_progress * max_scroll);
        } else if (phase_time > 11.0) {
            current_y = y + 0.02f - max_scroll;
        } else {
            current_y = y + 0.02f;
        }
        
        // Slide in / out effects stacked mathematically on the scroll offset
        if (phase_time < 1.0) {
            float t = phase_time;
            t = 1.0f - std::pow(1.0f - t, 3.0f);
            current_y = (y + 0.02f) + (1.0f - t) * (h * 0.4f);
            alpha = t;
        } else if (phase_time > 11.0) {
            float t = phase_time - 11.0;
            t = t * t * t;
            current_y = (y + 0.02f - max_scroll) - t * (h * 0.4f);
            alpha = 1.0f - t;
        }
    } else {
        // Fits perfectly, just slide in, pause, slide out
        if (phase_time < 1.0) {
            float t = phase_time; 
            t = 1.0f - std::pow(1.0f - t, 3.0f); 
            current_y = center_y_base + (1.0f - t) * (h * 0.4f);
            alpha = t;
        } else if (phase_time > 11.0) {
            float t = phase_time - 11.0; 
            t = t * t * t; 
            current_y = center_y_base - t * (h * 0.4f);
            alpha = 1.0f - t;
        }
    }
    
    // Scissor to prevent text vertically overlapping the weather / stock sections
    glEnable(GL_SCISSOR_TEST);
    int vp_x = static_cast<int>(x * renderer.width());
    // Move scissor down slightly to avoid cutting the "Headlines" header
    int vp_y = static_cast<int>((1.0f - (y + h)) * renderer.height());
    int vp_w = static_cast<int>(w * renderer.width());
    int vp_h = static_cast<int>((h - 0.02f) * renderer.height()); // Scissor only the content area
    glScissor(vp_x, vp_y, vp_w, vp_h);

    float draw_y = current_y;
    for (const auto& glyph_line : cache_.lines) {
        renderer.draw_text(glyph_line, x, draw_y, 1.0f, 0.8f, 0.8f, 0.8f, alpha);
        draw_y += line_height;
    }
    
    glDisable(GL_SCISSOR_TEST);
}

bool NewsModule::is_empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return headlines_.empty();
}

} // namespace nuc_display::modules
