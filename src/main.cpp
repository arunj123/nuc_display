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
#include "modules/container_reader.hpp"
#include "modules/input_module.hpp"
#include "modules/config_validator.hpp"
#include "modules/performance_monitor.hpp"

using namespace nuc_display;

std::atomic<bool> g_running{true};
std::atomic<bool> g_screenshot_requested{false};

void sigint_handler(int) {
    g_running = false;
}

void sigusr1_handler(int) {
    g_screenshot_requested = true;
}

int main() {
    std::signal(SIGINT, sigint_handler);
    std::signal(SIGTERM, sigint_handler);
    std::signal(SIGUSR1, sigusr1_handler);

    std::cout << "Starting NUC Display Engine (C++23 Modernized)...\n";

    // 1. Initialize Display Manager
    bool headless_mode = false;
    auto dm_result = core::DisplayManager::create();
    if (!dm_result) {
        if (dm_result.error() == core::DisplayError::DrmConnectorFailed) {
            std::cerr << "[Core] No display connected. Entering Headless Mode (Logic Only).\n";
            headless_mode = true;
        } else {
            std::cerr << "[Core] Failed to initialize Display Manager: " 
                      << core::error_to_string(dm_result.error()) << "\n";
            return 1;
        }
    }
    
    std::unique_ptr<core::DisplayManager> display;
    if (!headless_mode) {
        display = std::move(dm_result.value());
        std::cout << "[Core] Display Engine Running at " << display->width() << "x" << display->height() << "\n";
    }

    // 1.5 Load Configuration
    auto config_module = std::make_unique<modules::ConfigModule>();
    auto app_config_res = config_module->load_or_create_config("config.json");
    if (!app_config_res) {
        std::cerr << "[Core] Fatal Config Error! Cannot proceed.\n";
        return 1;
    }
    auto app_config = app_config_res.value();

    // 1.6 Validate Configuration
    auto config_errors = modules::ConfigValidator::validate(app_config);
    if (!config_errors.empty()) {
        std::cerr << "[Config] " << config_errors.size() << " validation error(s):\n";
        for (const auto& err : config_errors) {
            std::cerr << "  - " << err << "\n";
        }
    }

    // 2. Initialize Thread Pool
    utils::ThreadPool thread_pool(4);
    std::cout << "[Core] Initialized Thread Pool.\n";

    // 3. Initialize Modular Components
    auto renderer = std::make_unique<core::Renderer>();
    if (!headless_mode) {
        renderer->init(display->width(), display->height());
        
        // Correction for flipped/rotated display as reported by user
        // We can adjust these values if needed (0, 90, 180, 270)
        renderer->set_rotation(0); 
        renderer->set_flip(false, false); 
    }
    
    // Text Rendering
    auto text_renderer = std::make_unique<modules::TextRenderer>();
    if (auto res = text_renderer->load("assets/fonts/ubuntu.ttf"); !res) {
        std::cerr << "[Core] Failed to load Ubuntu font. Text rendering will fail.\n";
    }

    // Weather Module
    auto weather_module = std::make_unique<modules::WeatherModule>();
    std::optional<modules::WeatherData> weather_data;
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

    // Video Decoding (Hardware Accelerated) - Multi-instance support
    std::vector<std::unique_ptr<modules::VideoDecoder>> video_decoders;
    std::vector<bool> video_started; // Track if key-triggered videos have been started
    for (const auto& v_config : app_config.videos) {
        if (!v_config.enabled) continue;
        
        auto decoder = std::make_unique<modules::VideoDecoder>();
        if (display) {
            decoder->init_vaapi(display->drm_fd());
        }
        decoder->set_audio_enabled(v_config.audio_enabled);
        if (v_config.audio_enabled) {
            decoder->init_audio(v_config.audio_device);
        }
        
        if (!v_config.playlists.empty()) {
            // Only auto-start if start_trigger is "auto" (key == 0)
            if (v_config.start_trigger_key == 0) {
                decoder->load_playlist(v_config.playlists);
                video_started.push_back(true);
            } else {
                std::cout << "[Core] Video region waiting for key '" 
                          << v_config.start_trigger_name << "' to start.\n";
                video_started.push_back(false);
            }
            video_decoders.push_back(std::move(decoder));
        } else {
            std::cerr << "[Core] No videos defined for a configured video region.\n";
            video_started.push_back(false);
        }
    }

    // Container Reader
    auto container_reader = std::make_unique<modules::ContainerReader>();

    std::cout << "[Modules] All modular components initialized (Architecture Ready).\n";

    // 4. Initial Weather Fetch
    auto weather_task = thread_pool.enqueue([&weather_module, &config_module, addr = app_config.location.address]() {
        auto geo = config_module->geocode_address(addr);
        if (geo) {
            return weather_module->fetch_current_weather(geo.value().lat, geo.value().lon, geo.value().resolved_name);
        } else {
            return weather_module->fetch_current_weather(49.4521f, 11.0767f, "Nürnberg");
        }
    });

    auto stock_task = thread_pool.enqueue([&stock_module]() {
        stock_module->update_all_data();
    });

    auto news_task = thread_pool.enqueue([&news_module]() {
        news_module->update_headlines();
    });

    // Performance Monitor
    auto perf_monitor = std::make_unique<modules::PerformanceMonitor>();

    // Input Module (Keyboard)
    auto input_module = std::make_unique<modules::InputModule>();
    input_module->start();

    auto last_weather_update = std::chrono::steady_clock::now();
    auto last_stock_update = std::chrono::steady_clock::now();
    auto last_news_update = std::chrono::steady_clock::now();
    auto last_perf_update = std::chrono::steady_clock::now();
    int page_flip_failure_count = 0;
    auto program_start_time = std::chrono::steady_clock::now();

    bool weather_online = true;
    bool stock_online = true;
    bool news_online = true;

    // Multi-video background tasks
    std::vector<std::future<std::expected<void, modules::MediaError>>> video_process_tasks(video_decoders.size());
    bool videos_hidden = false;
    auto last_config_error_log = std::chrono::steady_clock::now();

    std::cout << "--- Starting main loop ---" << std::endl;

    while (g_running) {
        // --- POLL INPUT EVENTS ---
        while (auto event = input_module->pop_event()) {
            if (event->value != 1) continue; // Only handle KEY_DOWN
            uint16_t code = event->code;

            // Global: Hide/Show toggle
            if (app_config.global_keys.hide_videos && code == *app_config.global_keys.hide_videos) {
                videos_hidden = !videos_hidden;
                std::cout << "[Core] Videos " << (videos_hidden ? "HIDDEN" : "SHOWN") << "\n";
            }

            // Per-video key handling
            for (size_t i = 0; i < video_decoders.size(); ++i) {
                auto& decoder = video_decoders[i];
                auto& v_config = app_config.videos[i];

                // Start trigger
                if (v_config.start_trigger_key > 0 && code == v_config.start_trigger_key && !video_started[i]) {
                    std::cout << "[Core] Key trigger: Starting video " << i << "\n";
                    decoder->load_playlist(v_config.playlists);
                    video_started[i] = true;
                }

                // Navigation keys
                if (v_config.keys.next && code == *v_config.keys.next) {
                    std::cout << "[Core] Key: Next video for decoder " << i << "\n";
                    decoder->next_video();
                }
                if (v_config.keys.prev && code == *v_config.keys.prev) {
                    std::cout << "[Core] Key: Prev video for decoder " << i << "\n";
                    decoder->prev_video();
                }
                if (v_config.keys.skip_forward && code == *v_config.keys.skip_forward) {
                    std::cout << "[Core] Key: Skip forward for decoder " << i << "\n";
                    decoder->skip_forward(2.0);
                }
                if (v_config.keys.skip_backward && code == *v_config.keys.skip_backward) {
                    std::cout << "[Core] Key: Skip backward for decoder " << i << "\n";
                    decoder->skip_backward(2.0);
                }
            }

            // Stock navigation keys
            if (app_config.stock_keys.next_stock && code == *app_config.stock_keys.next_stock) {
                stock_module->next_stock();
            }
            if (app_config.stock_keys.prev_stock && code == *app_config.stock_keys.prev_stock) {
                stock_module->prev_stock();
            }
            if (app_config.stock_keys.next_chart && code == *app_config.stock_keys.next_chart) {
                stock_module->next_chart();
            }
            if (app_config.stock_keys.prev_chart && code == *app_config.stock_keys.prev_chart) {
                stock_module->prev_chart();
            }
        }
        
        auto now_p = std::chrono::steady_clock::now();
        double render_time_sec = std::chrono::duration<double>(now_p - program_start_time).count();

        // --- CHECK WEATHER UPDATES (Every 10 mins) ---
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::minutes>(now - last_weather_update).count() >= 10) {
            weather_task = thread_pool.enqueue([&weather_module, &config_module, addr = app_config.location.address]() {
                // Resolve name and coordinates dynamically for accuracy
                auto geo = config_module->geocode_address(addr);
                if (geo) {
                    return weather_module->fetch_current_weather(geo.value().lat, geo.value().lon, geo.value().resolved_name);
                } else {
                    return weather_module->fetch_current_weather(49.4521f, 11.0767f, "Nürnberg");
                }
            });
            last_weather_update = now;
        }

        if (weather_task.valid() && weather_task.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto result = weather_task.get();
            if (result) {
                weather_data = result.value();
                weather_online = true;
                std::cout << "[Weather] Updated: " << weather_data->temperature << "°C, " << weather_data->description << "\n";
            } else {
                weather_online = false;
                std::cerr << "[Weather] Update failed (Network Error)\n";
            }
        }

        // --- CHECK STOCK UPDATES (Every 5 mins, or 30s if failed) ---
        bool stocks_empty = stock_module->is_empty();
        int stock_retry_min = stocks_empty ? 0 : 5;
        int stock_retry_sec = stocks_empty ? 30 : 0;
        
        if (std::chrono::duration_cast<std::chrono::minutes>(now - last_stock_update).count() >= stock_retry_min &&
            std::chrono::duration_cast<std::chrono::seconds>(now - last_stock_update).count() >= stock_retry_sec) {
            stock_task = thread_pool.enqueue([&stock_module]() {
                stock_module->update_all_data();
            });
            last_stock_update = now;
        }

        if (stock_task.valid() && stock_task.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                stock_task.get();
                stock_online = true;
                // std::cout << "[Stock] Data Updated.\n";
            } catch (...) {
                stock_online = false;
                std::cerr << "[Stock] Update failed\n";
            }
        }

        // --- CHECK NEWS UPDATES (Every 15 mins, or 60s if failed) ---
        bool news_empty = news_module->is_empty();
        int news_retry_min = news_empty ? 0 : 15;
        int news_retry_sec = news_empty ? 60 : 0;

        if (std::chrono::duration_cast<std::chrono::minutes>(now - last_news_update).count() >= news_retry_min &&
            std::chrono::duration_cast<std::chrono::seconds>(now - last_news_update).count() >= news_retry_sec) {
            news_task = thread_pool.enqueue([&news_module]() {
                news_module->update_headlines();
            });
            last_news_update = now;
        }
        if (news_task.valid() && news_task.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                news_task.get();
                news_online = true;
            } catch (...) {
                news_online = false;
            }
        }

        // --- CHECK PERFORMANCE LOG (Every 30s) ---
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_perf_update).count() >= 30) {
            perf_monitor->update();
            perf_monitor->log();
            last_perf_update = now;
        }

        // --- RENDER DASHBOARD ---
        // (render_time_sec is calculated at loop start)
        
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
            
            // Retry weather faster when offline (every 10s)
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_weather_update).count() >= 10) {
                weather_task = thread_pool.enqueue([&weather_module, &config_module, addr = app_config.location.address]() {
                    auto geo = config_module->geocode_address(addr);
                    if (geo) {
                        return weather_module->fetch_current_weather(geo.value().lat, geo.value().lon, geo.value().resolved_name);
                    } else {
                        return weather_module->fetch_current_weather(49.4521f, 11.0767f, "Nürnberg");
                    }
                });
                last_weather_update = now;
            }

            // Periodically log config errors if running degraded
            if (!config_errors.empty() && std::chrono::duration_cast<std::chrono::seconds>(now - last_config_error_log).count() >= 30) {
                std::cerr << "[Config] Reminder: " << config_errors.size() << " validation error(s) present.\n";
                last_config_error_log = now;
            }
        }

        // Status Indicators for the User
        if (!weather_online || !stock_online || !news_online) {
            text_renderer->set_pixel_size(0, 18);
            if (auto glyphs = text_renderer->shape_text("Network Trouble: Reconnecting...")) {
                renderer->draw_text(glyphs.value(), 0.42f, 0.96f, 1.0f, 1.0f, 0.4f, 0.4f, 0.8f);
            }
        }

        // Stocks and News render independently — they have their own data/placeholders
        stock_module->render(*renderer, *text_renderer, render_time_sec);
        news_module->render(*renderer, *text_renderer, 0.03f, 0.80f, 0.36f, 0.18f, render_time_sec);

        // Hardware Accelerated Video Playback (Multi-Region)
        for (size_t i = 0; i < video_decoders.size(); ++i) {
            auto& decoder = video_decoders[i];
            auto& v_config = app_config.videos[i];
            auto& task = video_process_tasks[i];

            if (!task.valid() || task.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                if (task.valid()) {
                    auto res = task.get();
                    if (!res) {
                        // std::cerr << "[Video " << i << "] Error: " << (int)res.error() << "\n";
                    }
                }
                
                // Only process decoding if the video is started and not hidden
                if (video_started[i] && !videos_hidden && decoder->is_loaded()) {
                    task = thread_pool.enqueue([&decoder, render_time_sec]() {
                        return decoder->process(render_time_sec);
                    });
                }
            }

            if (!headless_mode && !videos_hidden && video_started[i] && decoder->is_loaded()) {
                bool playing = decoder->render(*renderer, display->egl_display(), 
                                               v_config.src_x, v_config.src_y,
                                               v_config.src_w, v_config.src_h,
                                               v_config.x, v_config.y, 
                                               v_config.w, v_config.h, 
                                               render_time_sec);
                if (!playing) {
                    if (task.valid()) task.get();
                    decoder->next_video();
                }
            }
        }
        // ALSA is now processed iteratively inside video_decoder->render() via packet interleaving

        // Manual screenshot trigger via SIGUSR1
        if (g_screenshot_requested) {
            if (auto cap_res = screenshot_module->capture(display->width(), display->height()); cap_res) {
                screenshot_module->save("manual_screenshot.png");
                std::cout << "[Core] Manual screenshot saved to manual_screenshot.png\n";
            }
            g_screenshot_requested = false;
        }

        // --- SWAP BUFFERS ---
        if (!headless_mode) {
            display->swap_buffers();

            // --- PAGE FLIP ---
            if (!display->page_flip()) {
                page_flip_failure_count++;
                if (page_flip_failure_count % 60 == 0) {
                    std::cerr << "[Core] Warning: DRM Page Flip failed " << page_flip_failure_count << " times consecutively. "
                              << "This usually means DRM master was lost or a process conflict exists.\n";
                }
                if (page_flip_failure_count > 600) {
                    std::cerr << "[Core] Fatal: DRM Page Flip persistent failure. Exiting.\n";
                    g_running = false;
                }
                
                // Transient failure (e.g., after screenshot). Don't exit — just skip this frame.
                // Still process DRM events in case a flip was already pending.
                display->process_drm_events(16);
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                continue;
            }
            page_flip_failure_count = 0; // Reset on success

            // --- PROCESS KMS EVENTS (VSYNC) ---
            display->process_drm_events(100); 
        } else {
            // Headless sleep to prevent pinning CPU
            std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30fps heartbeat
        }
    }

    std::cout << "\n[Core] Shutting down gracefully...\n";
    return 0;
}
