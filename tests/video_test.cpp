#include <gtest/gtest.h>
#include "modules/video_decoder.hpp"
#include "modules/container_reader.hpp"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>

using namespace nuc_display::modules;

// Helper: open DRM device and init VA-API on a decoder, or skip
static int setup_vaapi(VideoDecoder& decoder) {
    int drm_fd = open("/dev/dri/renderD128", O_RDWR);
    if (drm_fd >= 0) {
        auto init_res = decoder.init_vaapi(drm_fd);
        if (!init_res) {
            close(drm_fd);
            return -1;
        }
    }
    return drm_fd;
}

TEST(VideoDecoderTest, LoadAndProcessVideoOnly) {
    VideoDecoder decoder;
    int drm_fd = setup_vaapi(decoder);
    if (drm_fd < 0) GTEST_SKIP() << "VA-API or DRM not available";

    auto result = decoder.load("sample_no_audio.mp4");
    if (!result.has_value()) {
        close(drm_fd);
        GTEST_SKIP() << "Failed to load sample_no_audio.mp4";
    }

    // Decode a few frames
    for (int i = 0; i < 5; ++i) {
        decoder.process(i * 0.033);
    }
    close(drm_fd);
}

TEST(VideoDecoderTest, LoadAndProcessVideoWithAudio) {
    VideoDecoder decoder;
    int drm_fd = setup_vaapi(decoder);
    if (drm_fd < 0) GTEST_SKIP() << "VA-API or DRM not available";

    decoder.set_audio_enabled(true);
    // Don't actually init ALSA in CI — just verify the decoder handles
    // audio streams without crashing (audio output disabled).

    auto result = decoder.load("sample_with_audio.mp4");
    if (!result.has_value()) {
        close(drm_fd);
        GTEST_SKIP() << "Failed to load sample_with_audio.mp4";
    }

    for (int i = 0; i < 5; ++i) {
        decoder.process(i * 0.033);
    }
    close(drm_fd);
}

TEST(VideoDecoderTest, PlaylistLoadAndCycle) {
    VideoDecoder decoder;
    int drm_fd = setup_vaapi(decoder);
    if (drm_fd < 0) GTEST_SKIP() << "VA-API or DRM not available";

    // Load a playlist with two videos
    std::vector<std::string> playlist = {"sample_no_audio.mp4", "sample_with_audio.mp4"};
    decoder.load_playlist(playlist);

    // Decode a few frames from first video
    for (int i = 0; i < 3; ++i) {
        decoder.process(i * 0.033);
    }

    // Advance to next video in playlist
    decoder.next_video();

    // Decode a few frames from second video (should not crash)
    for (int i = 0; i < 3; ++i) {
        decoder.process(i * 0.033);
    }

    // Cycle back to first video (loop test)
    decoder.next_video();

    for (int i = 0; i < 3; ++i) {
        decoder.process(i * 0.033);
    }

    close(drm_fd);
}

TEST(ContainerReaderTest, OpenValidFile) {
    ContainerReader reader;
    auto result = reader.open("sample.mp4");
    EXPECT_TRUE(result.has_value());

    int video_idx = reader.find_video_stream();
    EXPECT_GE(video_idx, 0);
}

TEST(ContainerReaderTest, OpenFileWithAudio) {
    ContainerReader reader;
    auto result = reader.open("sample_with_audio.mp4");
    EXPECT_TRUE(result.has_value());

    int video_idx = reader.find_video_stream();
    EXPECT_GE(video_idx, 0);

    int audio_idx = reader.find_audio_stream();
    EXPECT_GE(audio_idx, 0);
}

TEST(ContainerReaderTest, OpenFileNoAudio) {
    ContainerReader reader;
    auto result = reader.open("sample_no_audio.mp4");
    EXPECT_TRUE(result.has_value());

    int video_idx = reader.find_video_stream();
    EXPECT_GE(video_idx, 0);

    int audio_idx = reader.find_audio_stream();
    EXPECT_EQ(audio_idx, -1); // Should not have audio
}

TEST(ContainerReaderTest, ReopenFile) {
    ContainerReader reader;
    auto result = reader.open("sample.mp4");
    EXPECT_TRUE(result.has_value());

    // Re-open a different file (should not crash)
    auto result2 = reader.open("sample_no_audio.mp4");
    EXPECT_TRUE(result2.has_value());

    int video_idx = reader.find_video_stream();
    EXPECT_GE(video_idx, 0);
}

TEST(ContainerReaderTest, OpenInvalidFile) {
    ContainerReader reader;
    auto result = reader.open("nonexistent.mp4");
    EXPECT_FALSE(result.has_value());
}
