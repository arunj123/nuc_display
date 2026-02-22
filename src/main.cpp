#include <iostream>
#include <csignal>
#include <atomic>
#include <cmath>
#include <thread>
#include <chrono>
#include <memory>
#include <sstream>
#include <iomanip>
#include <vector>

#include "core/display_manager.hpp"
#include "core/renderer.hpp"
#include "utils/thread_pool.hpp"
#include "modules/image_loader.hpp"
#include "modules/text_renderer.hpp"
#include "modules/weather_module.hpp"
#include "modules/config_module.hpp"
#include "modules/stock_module.hpp"
#include "modules/screenshot_module.hpp"
#include "modules/news_module.hpp"
#include "modules/video_decoder.hpp"
#include "modules/audio_player.hpp"
#include "modules/container_reader.hpp"

using namespace nuc_display;

std::atomic<bool> g_running{true};

void sigint_handler(int) {
    g_running = false;
}

int main() {
    std::signal(SIGINT, sigint_handler);
    std::signal(SIGTERM, sigint_handler);

    std::cout << "Starting NUC Display Engine (C++23 Modernized)...\n";

    // 1. Initialize Display Manager
    auto dm_result = core::DisplayManager::create();
    if (!dm_result) {
        std::cerr << "[Core] Failed to initialize Display Manager: " 
                  << core::error_to_string(dm_result.error()) << "\n";
        return 1;
    }
    auto display = std::move(dm_result.value());
    std::cout << "[Core] Display Engine Running at " << display->width() << "x" << display->height() << "\n";

    // 1.5 Load Configuration
    auto config_module = std::make_unique<modules::ConfigModule>();
    auto config_res = config_module->load_or_create_config("config.json");
    if (!config_res) {
        std::cerr << "[Core] Fatal Config Error! Cannot proceed.\n";
        return 1;
    }
    auto app_config = config_res.value();

    // 2. Initialize Thread Pool
    utils::ThreadPool thread_pool(4);
    std::cout << "[Core] Initialized Thread Pool.\n";

    // 3. Initialize Modular Components
    auto renderer = std::make_unique<core::Renderer>();
    renderer->init(display->width(), display->height());
    
    // Correction for flipped/rotated display as reported by user
    // We can adjust these values if needed (0, 90, 180, 270)
    renderer->set_rotation(0); 
    renderer->set_flip(false, false); 
    
    // Text Rendering
    auto text_renderer = std::make_unique<modules::TextRenderer>();
    if (auto res = text_renderer->load("assets/fonts/ubuntu.ttf"); !res) {
        std::cerr << "[Core] Failed to load Ubuntu font. Text rendering will fail.\n";
    }

    // Weather Module
    auto weather_module = std::make_unique<modules::WeatherModule>();
    std::optional<modules::WeatherData> weather_data;
    bool screenshot_taken = false;
    auto screenshot_module = std::make_unique<modules::ScreenshotModule>();
    
    // Stock Module
    auto stock_module = std::make_unique<modules::StockModule>();
    for (const auto& s : app_config.stocks) {
        stock_module->add_symbol(s.symbol, s.name, s.currency_symbol);
    }

    // News Module
    auto news_module = std::make_unique<modules::NewsModule>();

    // Image Loading
    auto image_loader = std::make_unique<modules::ImageLoader>();

    // Video Decoding (Hardware Accelerated)
    auto video_decoder = std::make_unique<modules::VideoDecoder>();
    video_decoder->init_vaapi(display->drm_fd());

    // Audio Playback
    auto audio_player = std::make_unique<modules::AudioPlayer>();
    // audio_player->init_alsa("default"); // ALSA device initialization

    // Container Reader
    auto container_reader = std::make_unique<modules::ContainerReader>();

    std::cout << "[Modules] All modular components initialized (Architecture Ready).\n";

    // 4. Initial Weather Fetch
    auto weather_task = thread_pool.enqueue([&weather_module]() {
        // Fetch for London or a configurable location
        return weather_module->fetch_current_weather(51.5074f, -0.1278f);
    });

    auto stock_task = thread_pool.enqueue([&stock_module]() {
        stock_module->update_all_data();
    });

    auto news_task = thread_pool.enqueue([&news_module]() {
        news_module->update_headlines();
    });

    auto last_weather_update = std::chrono::steady_clock::now();
    auto last_stock_update = std::chrono::steady_clock::now();
    auto last_news_update = std::chrono::steady_clock::now();
    auto program_start_time = std::chrono::steady_clock::now();

    float color_offset = 0.0f;

    std::cout << "--- Starting main loop ---\n";

    while (g_running) {
        // --- CHECK WEATHER UPDATES (Every 10 mins) ---
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::minutes>(now - last_weather_update).count() >= 10) {
            weather_task = thread_pool.enqueue([&weather_module, lat = app_config.location.lat, lon = app_config.location.lon]() {
                return weather_module->fetch_current_weather(lat, lon);
            });
            last_weather_update = now;
        }

        if (weather_task.valid() && weather_task.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto result = weather_task.get();
            if (result) {
                weather_data = result.value();
                std::cout << "[Weather] Updated: " << weather_data->temperature << "°C, " << weather_data->description << "\n";
            }
        }

        // --- CHECK STOCK UPDATES (Every 5 mins) ---
        if (std::chrono::duration_cast<std::chrono::minutes>(now - last_stock_update).count() >= 5) {
            stock_task = thread_pool.enqueue([&stock_module]() {
                stock_module->update_all_data();
            });
            last_stock_update = now;
        }

        if (stock_task.valid() && stock_task.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            stock_task.get();
            std::cout << "[Stock] Data Updated.\n";
        }

        // --- CHECK NEWS UPDATES (Every 15 mins) ---
        if (std::chrono::duration_cast<std::chrono::minutes>(now - last_news_update).count() >= 15) {
            news_task = thread_pool.enqueue([&news_module]() {
                news_module->update_headlines();
            });
            last_news_update = now;
        }
        if (news_task.valid() && news_task.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            news_task.get();
        }

        // --- RENDER DASHBOARD ---
        double render_time_sec = std::chrono::duration<double>(now - program_start_time).count();
        
        if (weather_data) {
            weather_module->render(*renderer, *text_renderer, weather_data.value(), render_time_sec);
        } else {
            // Offline placeholder: show time, date, separator + "Waiting for data..."
            renderer->clear(0.05f, 0.05f, 0.07f, 1.0f);
            
            // Draw vertical separator even in offline mode
            std::vector<float> sep_pts = { 0.405f, 0.03f, 0.405f, 0.97f };
            renderer->draw_line_strip(sep_pts, 0.2f, 0.2f, 0.25f, 0.6f, 1.0f);
            
            // Time & Date (always available — uses system clock)
            auto wall_now = std::chrono::system_clock::now();
            std::time_t now_c = std::chrono::system_clock::to_time_t(wall_now);
            struct tm *parts = std::localtime(&now_c);
            
            std::stringstream time_ss;
            time_ss << std::put_time(parts, "%H:%M");
            text_renderer->set_pixel_size(0, 100);
            if (auto glyphs = text_renderer->shape_text(time_ss.str())) {
                renderer->draw_text(glyphs.value(), 0.03f, 0.10f, 1.0f);
            }
            
            std::stringstream date_ss;
            date_ss << std::put_time(parts, "%a, %b %d");
            text_renderer->set_pixel_size(0, 28);
            if (auto glyphs = text_renderer->shape_text(date_ss.str())) {
                renderer->draw_text(glyphs.value(), 0.03f, 0.15f, 1.0f, 0.5f, 0.5f, 0.5f, 1.0f);
            }
            
            // Placeholder messages
            text_renderer->set_pixel_size(0, 32);
            if (auto glyphs = text_renderer->shape_text("Waiting for weather data...")) {
                renderer->draw_text(glyphs.value(), 0.03f, 0.45f, 1.0f, 0.4f, 0.4f, 0.4f, 1.0f);
            }
            if (auto glyphs = text_renderer->shape_text("Waiting for stock data...")) {
                renderer->draw_text(glyphs.value(), 0.42f, 0.45f, 1.0f, 0.4f, 0.4f, 0.4f, 1.0f);
            }
            
            // Retry weather faster when we have no data at all (every 30s instead of 10min)
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_weather_update).count() >= 30) {
                weather_task = thread_pool.enqueue([&weather_module, lat = app_config.location.lat, lon = app_config.location.lon]() {
                    return weather_module->fetch_current_weather(lat, lon);
                });
                last_weather_update = now;
            }
        }

        // Stocks and News render independently — they have their own data/placeholders
        stock_module->render(*renderer, *text_renderer, render_time_sec);
        news_module->render(*renderer, *text_renderer, 0.03f, 0.80f, 0.36f, 0.18f, render_time_sec);

        // Take a screenshot once for verification after APIs resolve
        if (!screenshot_taken && render_time_sec > 4.0) {
            if (auto cap_res = screenshot_module->capture(display->width(), display->height()); cap_res) {
                screenshot_module->save("debug_weather.png");
                screenshot_taken = true;
                std::cout << "[Core] Debug screenshot saved.\n";
            }
        }

        // --- SWAP BUFFERS ---
        display->swap_buffers();

        // --- PAGE FLIP ---
        if (!display->page_flip()) {
            break;
        }

        // --- PROCESS KMS EVENTS (VSYNC) ---
        display->process_drm_events(100); 
    }

    std::cout << "\n[Core] Shutting down gracefully...\n";
    return 0;
}
