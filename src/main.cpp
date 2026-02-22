#include <iostream>
#include <csignal>
#include <atomic>
#include <cmath>
#include <thread>
#include <chrono>
#include <memory>

#include "core/display_manager.hpp"
#include "core/renderer.hpp"
#include "utils/thread_pool.hpp"
#include "modules/image_loader.hpp"
#include "modules/text_renderer.hpp"
#include "modules/weather_module.hpp"
#include "modules/screenshot_module.hpp"
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

    auto last_weather_update = std::chrono::steady_clock::now();

    float color_offset = 0.0f;

    std::cout << "--- Starting main loop ---\n";

    while (g_running) {
        // --- CHECK WEATHER UPDATES (Every 10 mins) ---
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::minutes>(now - last_weather_update).count() >= 10) {
            weather_task = thread_pool.enqueue([&weather_module]() {
                return weather_module->fetch_current_weather(51.5074f, -0.1278f);
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

        // --- RENDER WEATHER DASHBOARD ---
        if (weather_data) {
            weather_module->render(*renderer, *image_loader, *text_renderer, weather_data.value());
            
            // Take a screenshot once for verification
            if (!screenshot_taken && weather_data) {
                if (auto cap_res = screenshot_module->capture(display->width(), display->height()); cap_res) {
                    screenshot_module->save("debug_weather.png");
                    screenshot_taken = true;
                    std::cout << "[Core] Debug screenshot saved.\n";
                }
            }
        } else {
            renderer->clear(0.1f, 0.1f, 0.1f, 1.0f);
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
