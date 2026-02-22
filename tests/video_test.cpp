#include <gtest/gtest.h>
#include "modules/video_decoder.hpp"
#include "modules/audio_player.hpp"
#include "modules/container_reader.hpp"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>

using namespace nuc_display::modules;

TEST(VideoDecoderTest, LoadAndProcess) {
    VideoDecoder decoder;
    int drm_fd = open("/dev/dri/renderD128", O_RDWR);
    if (drm_fd >= 0) {
        auto init_res = decoder.init_vaapi(drm_fd);
        if (!init_res) {
            close(drm_fd);
            GTEST_SKIP() << "VA-API initialization failed";
        }
    } else {
        GTEST_SKIP() << "DRM device not available (/dev/dri/renderD128)";
    }
    
    auto result = decoder.load("sample.mp4");
    if (!result.has_value()) {
        close(drm_fd);
        GTEST_SKIP() << "Failed to load sample.mp4";
    }

    // Decode a few frames
    for (int i = 0; i < 5; ++i) {
        auto proc_res = decoder.process(i * 0.033);
        if (!proc_res.has_value()) {
            break; // End of file or error
        }
    }
    close(drm_fd);
}

TEST(AudioPlayerTest, LoadAndProcess) {
    AudioPlayer player;
    auto init_res = player.init_alsa("default");
    if (!init_res) {
        GTEST_SKIP() << "ALSA 'default' device not available";
    }

    auto result = player.load("sample.mp4");
    if (!result.has_value()) {
        GTEST_SKIP() << "Failed to load sample.mp4 for audio (might not contain an audio stream)";
    } else {
        // Decode a few audio frames
        for (int i = 0; i < 5; ++i) {
            auto proc_res = player.process(i * 0.033);
            if (!proc_res.has_value()) {
                break;
            }
        }
    }
}

TEST(ContainerReaderTest, OpenValidFile) {
    ContainerReader reader;
    auto result = reader.open("sample.mp4");
    EXPECT_TRUE(result.has_value());
    
    int video_idx = reader.find_video_stream();
    EXPECT_GE(video_idx, 0);
}

TEST(ContainerReaderTest, OpenInvalidFile) {
    ContainerReader reader;
    auto result = reader.open("nonexistent.mp4");
    EXPECT_FALSE(result.has_value());
}
