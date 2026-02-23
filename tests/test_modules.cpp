#include <gtest/gtest.h>
#include "modules/image_loader.hpp"
#include "modules/text_renderer.hpp"

using namespace nuc_display::modules;

#include <fstream>

TEST(ImageLoaderTest, UnsupportedFormat) {
    std::ofstream out("dummy.txt");
    out << "Not an image";
    out.close();

    ImageLoader loader;
    auto result = loader.load("dummy.txt");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), MediaError::UnsupportedFormat);
    
    std::remove("dummy.txt");
}

TEST(ImageLoaderTest, FileNotFound) {
    ImageLoader loader;
    auto result = loader.load("nonexistent_image.jpg");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), MediaError::FileNotFound);
}

TEST(TextRendererTest, Initialization) {
    // This might fail if FreeType isn't available in the environment,
    // but the constructor should at least run.
    TextRenderer renderer;
}

TEST(TextRendererTest, ShapeEmptyText) {
    TextRenderer renderer;
    // Note: This requires a font to be loaded to actually work.
    // So we just check that it handles missing font gracefully.
    auto result = renderer.shape_text("Hello");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), MediaError::InternalError);
}

#include "modules/weather_module.hpp"

TEST(WeatherModuleTest, DescriptionAndIconMapping) {
    WeatherModule module;
    EXPECT_EQ(module.get_weather_description(0), "Clear sky");
    EXPECT_EQ(module.get_weather_icon_filename(0), "assets/weather/clear.png");
    
    // Check Storms
    EXPECT_EQ(module.get_weather_description(95), "Thunderstorm: Slight or moderate");
    EXPECT_EQ(module.get_weather_icon_filename(95), "assets/weather/storm.png");
    
    // Check Rain
    EXPECT_EQ(module.get_weather_description(65), "Rain: Slight, moderate and heavy intensity");
    EXPECT_EQ(module.get_weather_icon_filename(65), "assets/weather/rain.png");
    
    // Check Snow
    EXPECT_EQ(module.get_weather_description(71), "Snow fall: Slight, moderate, and heavy intensity");
    EXPECT_EQ(module.get_weather_icon_filename(71), "assets/weather/snow.png");

    EXPECT_EQ(module.get_weather_description(999), "Unknown");
    EXPECT_EQ(module.get_weather_icon_filename(999), "assets/weather/unknown.png");
}

#include "modules/stock_module.hpp"

TEST(StockModuleTest, InvalidSymbolHandled) {
    StockModule module;
    // Test that fetching/parsing an invalid symbol gracefully fails and doesn't crash
    module.add_symbol("INVALID_SYMBOL_ABC_123", "Invalid");
    module.update_all_data();
    
    // Check that we don't have crash and state is still valid
    SUCCEED();
}

#include "modules/config_module.hpp"
#include <nlohmann/json.hpp>

class ConfigModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_file_ = "test_config_" + std::to_string(std::rand()) + ".json";
    }

    void TearDown() override {
        std::remove(test_file_.c_str());
    }

    std::string test_file_;
};

TEST_F(ConfigModuleTest, CreateDefaultWhenFileMissing) {
    ConfigModule config;
    auto result = config.load_or_create_config(test_file_);
    ASSERT_TRUE(result.has_value());
    
    // Video config defaults Check
    ASSERT_FALSE(result->videos.empty());
    EXPECT_TRUE(result->videos[0].enabled);
    EXPECT_FALSE(result->videos[0].audio_enabled);
    EXPECT_EQ(result->videos[0].playlists[0], "tests/sample.mp4");
    EXPECT_FLOAT_EQ(result->videos[0].x, 0.70f);
    EXPECT_FLOAT_EQ(result->videos[0].src_w, 1.0f);
}

TEST_F(ConfigModuleTest, ParseValidConfig) {
    nlohmann::json j = {
        {"location", {{"address", "London, UK"}, {"lat", 51.5}, {"lon", -0.1}}},
        {"stocks", {{{"symbol", "AAPL"}, {"name", "Apple"}, {"currency_symbol", "$"}},
                    {{"symbol", "GOOG"}, {"name", "Alphabet"}, {"currency_symbol", "$"}}}},
        {"video", {
            {"enabled", false},
            {"audio_enabled", true},
            {"playlists", {"custom_video1.mp4", "custom_video2.mp4"}},
            {"x", 0.1f}, {"y", 0.2f}, {"w", 0.3f}, {"h", 0.4f},
            {"src_x", 0.1f}, {"src_y", 0.1f}, {"src_w", 0.8f}, {"src_h", 0.8f}
        }}
    };
    std::ofstream out(test_file_);
    out << j.dump();
    out.close();

    ConfigModule config;
    auto result = config.load_or_create_config(test_file_);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->location.address, "London, UK");
    EXPECT_EQ(result->stocks.size(), 2);
    ASSERT_FALSE(result->videos.empty());
    EXPECT_FALSE(result->videos[0].enabled);
    EXPECT_TRUE(result->videos[0].audio_enabled);
    EXPECT_EQ(result->videos[0].playlists.size(), 2);
    EXPECT_EQ(result->videos[0].playlists[0], "custom_video1.mp4");
    EXPECT_EQ(result->videos[0].playlists[1], "custom_video2.mp4");
    EXPECT_FLOAT_EQ(result->videos[0].x, 0.1f);
    EXPECT_FLOAT_EQ(result->videos[0].src_x, 0.1f);
    EXPECT_FLOAT_EQ(result->videos[0].src_w, 0.8f);
}

TEST_F(ConfigModuleTest, HandleCorruptedJson) {
    std::ofstream out(test_file_);
    out << "{ invalid_json: ";
    out.close();

    ConfigModule config;
    auto result = config.load_or_create_config(test_file_);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ConfigError::ParseError);
}

TEST_F(ConfigModuleTest, HandleMissingVideoNode) {
    nlohmann::json j = {
        {"location", {{"address", "London, UK"}, {"lat", 51.5}, {"lon", -0.1}}},
        {"stocks", nlohmann::json::array()}
    };
    std::ofstream out(test_file_);
    out << j.dump();
    out.close();

    ConfigModule config;
    auto result = config.load_or_create_config(test_file_);
    ASSERT_TRUE(result.has_value());
    // Should fallback to default video config and save
    ASSERT_FALSE(result->videos.empty());
    EXPECT_TRUE(result->videos[0].enabled);
    EXPECT_EQ(result->videos[0].playlists[0], "tests/sample.mp4");
}
