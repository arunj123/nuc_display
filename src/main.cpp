#include <iostream>
#include <csignal>
#include <atomic>
#include <cmath>
#include <thread>
#include <chrono>
#include <memory>

#include "core/display_manager.hpp"
#include "utils/thread_pool.hpp"
#include "modules/image_loader.hpp"
#include "modules/text_renderer.hpp"
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

    // 1. Initialize Thread Pool
    utils::ThreadPool thread_pool(4);
    std::cout << "[Core] Initialized Thread Pool.\n";

    // 2. Initialize Display Manager
    auto dm_result = core::DisplayManager::create();
    if (!dm_result) {
        std::cerr << "[Core] Failed to initialize Display Manager: " 
                  << core::error_to_string(dm_result.error()) << "\n";
        return 1;
    }
    auto display = std::move(dm_result.value());
    std::cout << "[Core] Display Engine Running at " << display->width() << "x" << display->height() << "\n";

    // 3. Initialize Modular Components (Architecture Demonstration)
    
    // Text Rendering
    auto text_renderer = std::make_unique<modules::TextRenderer>();
    // text_renderer->load("path/to/font.ttf"); // Layout only for now

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

    // 4. Example Asynchronous Operation
    auto future_media_task = thread_pool.enqueue([&image_loader]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        // In a real scenario, this would load a heavy resource
        // image_loader->load("test.jpg");
        return "Async Media Load Simulation Complete!";
    });

    float color_offset = 0.0f;

    std::cout << "--- Starting main loop ---\n";

    while (g_running) {
        // --- CHECK ASYNC TASKS ---
        if (future_media_task.valid() && future_media_task.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            std::cout << "[Async] " << future_media_task.get() << "\n";
        }

        // --- RENDER GL FRAME ---
        float r = 0.5f + 0.5f * std::sin(color_offset);
        float g = 0.5f + 0.5f * std::sin(color_offset + 2.094f);
        float b = 0.5f + 0.5f * std::sin(color_offset + 4.188f);
        color_offset += 0.01f;

        glClearColor(r, g, b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

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
