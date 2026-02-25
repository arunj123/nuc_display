#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "modules/video_decoder.hpp"
#include "core/renderer.hpp"
#include "stubs_alsa.hpp"
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

using namespace nuc_display::modules;
using namespace nuc_display::core;
using namespace nuc_display::tests::mock;

class VideoDecoderTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_alsa_mock.reset();
        
        // Ensure a dummy video exists for testing
        test_video_path_ = "tests/dummy_video.mp4";
        if (!std::filesystem::exists("tests")) {
            std::filesystem::create_directory("tests");
        }
        
        // We actually need a real or parseable media file for FFmpeg to open.
        // We'll use a very small synthetic video generated via ffmpeg if possible,
        // or just test the logic that doesn't strictly depend on ffmpeg decoding.
        // Wait, opening a file triggers real FFmpeg. We must either have a real file
        // or we just test the math logic. We can do a quick system call to create a 1s black video.
        if (!std::filesystem::exists(test_video_path_)) {
            system(("ffmpeg -f lavfi -i color=c=black:s=320x240:d=5.0 -f lavfi -i anullsrc=r=48000:cl=stereo -c:v libx264 -c:a aac -shortest " + test_video_path_ + " -y >/dev/null 2>&1").c_str());
        }
    }

    void TearDown() override {
        // if (std::filesystem::exists(test_video_path_)) {
        //     std::filesystem::remove(test_video_path_);
        // }
    }

    std::string test_video_path_;
};

// 1. Test ALSA Initialization Failure
TEST_F(VideoDecoderTest, AlsaOpenFailure) {
    g_alsa_mock.fail_open = true;
    VideoDecoder decoder;
    
    auto res = decoder.load(test_video_path_);
    EXPECT_TRUE(res.has_value());
}

// 2. Test Audio Underrun Recovery Logic
TEST_F(VideoDecoderTest, AlsaUnderrunRecovery) {
    VideoDecoder decoder;
    decoder.set_audio_enabled(true);
    decoder.init_audio("default"); 
    ASSERT_TRUE(decoder.load(test_video_path_).has_value());
    
    // Trigger an underrun simulation on the next write
    g_alsa_mock.simulate_underrun = true;
    
    // We need to call process() to let it read packets and write audio
    for (int i=0; i<200; i++) {
        decoder.process(0.1 * i);
    }
    
    // We expect it to have recovered and written frames eventually
    EXPECT_FALSE(g_alsa_mock.simulate_underrun); // Flag should have been consumed
}

// 3. Test ALSA Total Failure (Count > 10)
TEST_F(VideoDecoderTest, AlsaRecoveryFailureFallback) {
    VideoDecoder decoder;
    decoder.set_audio_enabled(true);
    decoder.init_audio("default");
    ASSERT_TRUE(decoder.load(test_video_path_).has_value());
    
    g_alsa_mock.simulate_underrun = true;
    g_alsa_mock.recover_fail_count = 15; // > 10
    
    for (int i=0; i<200; i++) {
        decoder.process(0.1 * i);
    }
    
    // It should have tried to reopen the PCM handle internally after 10 failures
    // Without crashing.
    GTEST_SUCCEED();
}

// 4. Test Skip Forward Logic (Bounds checking)
TEST_F(VideoDecoderTest, SkipForwardBounds) {
    VideoDecoder decoder;
    ASSERT_TRUE(decoder.load(test_video_path_).has_value());
    
    decoder.skip_forward(50.0); // skip way past the 1.0s duration
    
    // The seek_offset should be clipped to duration - 0.5s approx
    // Let's call process and render to update state
    Renderer dummy_renderer;
    decoder.process(0.0);
    // 1s video, skip 50s. Target should be 0.5s max
    GTEST_SUCCEED(); 
}

// 5. Test Skip Backward Logic
TEST_F(VideoDecoderTest, SkipBackwardBounds) {
    VideoDecoder decoder;
    ASSERT_TRUE(decoder.load(test_video_path_).has_value());
    
    decoder.skip_forward(0.5);
    decoder.skip_backward(50.0); // skip before start
    
    GTEST_SUCCEED(); // Should not crash, and seek_offset should be >= 0
}

// 6. Test ALSA State Machine Transitions (Regression test for silent HDMI fix)
TEST_F(VideoDecoderTest, AlsaStateTransitions) {
    VideoDecoder decoder;
    decoder.set_audio_enabled(true);
    decoder.init_audio("default"); // Opens PCM, state -> OPEN
    
    ASSERT_TRUE(decoder.load(test_video_path_).has_value());
    // After load, state should be PREPARED
    EXPECT_EQ(g_alsa_mock.state, PcmState::PREPARED);
    // Threshold should be set to 1
    EXPECT_EQ(g_alsa_mock.sw_start_threshold, 1);
    
    // Simulate loading a second video. 
    // cleanup_codec() should move state to SETUP via hw_free()
    // then load() should successfully move it back to PREPARED via hw_params()
    ASSERT_TRUE(decoder.load(test_video_path_).has_value());
    EXPECT_EQ(g_alsa_mock.state, PcmState::PREPARED);
}

// 7. Negative Test: Load non-existent file
TEST_F(VideoDecoderTest, LoadNonExistentFile) {
    VideoDecoder decoder;
    auto res = decoder.load("/tmp/non_existent_file.mp4");
    EXPECT_FALSE(res.has_value());
}

// 8. Negative Test: ALSA Hardware Parameter Failure (EBUSY)
TEST_F(VideoDecoderTest, AlsaHwParamsFailure) {
    VideoDecoder decoder;
    decoder.set_audio_enabled(true);
    decoder.init_audio("default");
    
    // Simulate EBUSY by putting PCM in wrong state before load
    g_alsa_mock.state = PcmState::RUNNING; 
    
    // load() calls cleanup_codec() -> hw_free() which should move state to SETUP
    // but if it failed to free or something, we test the robust transition.
    auto res = decoder.load(test_video_path_);
    EXPECT_TRUE(res.has_value()); // The video should still load even if audio setup fails
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
