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

#include "modules/config_validator.hpp"

TEST(ConfigValidatorTest, ValidConfigPasses) {
    AppConfig config;
    config.location = {"Test", 0.0f, 0.0f};
    config.stocks.push_back({"AAPL", "Apple", "$"});
    config.global_keys.hide_videos = 35; // KEY_H
    
    VideoConfig v1;
    v1.enabled = true;
    v1.playlists = {"file.mp4"};
    v1.start_trigger_key = 16; // KEY_Q
    v1.keys.next = 106; // KEY_RIGHT
    config.videos.push_back(v1);

    auto errors = ConfigValidator::validate(config);
    EXPECT_TRUE(errors.empty());
}

TEST(ConfigValidatorTest, DuplicateKeyDetected) {
    AppConfig config;
    config.location = {"Test", 0.0f, 0.0f};
    config.stocks.push_back({"AAPL", "Apple", "$"});
    config.global_keys.hide_videos = 35; // KEY_H
    
    VideoConfig v1;
    v1.enabled = true;
    v1.playlists = {"file.mp4"};
    v1.start_trigger_key = 35; // Duplicate with global_keys!
    config.videos.push_back(v1);

    auto errors = ConfigValidator::validate(config);
    ASSERT_EQ(errors.size(), 1);
    EXPECT_NE(errors[0].find("Duplicate key binding"), std::string::npos);
}

TEST(ConfigValidatorTest, MissingPlaylist) {
    AppConfig config;
    config.location = {"Test", 0.0f, 0.0f};
    config.stocks.push_back({"AAPL", "Apple", "$"});
    
    VideoConfig v1;
    v1.enabled = true;
    v1.playlists.clear(); // Empty playlist!
    config.videos.push_back(v1);

    auto errors = ConfigValidator::validate(config);
    ASSERT_EQ(errors.size(), 1);
    EXPECT_NE(errors[0].find("has no playlists"), std::string::npos);
}

TEST(ConfigValidatorTest, OutOfRangeCoordinates) {
    AppConfig config;
    config.location = {"Test", 0.0f, 0.0f};
    config.stocks.push_back({"AAPL", "Apple", "$"});
    
    VideoConfig v1;
    v1.enabled = true;
    v1.playlists = {"file.mp4"};
    v1.x = 1.5f; // Invalid!
    config.videos.push_back(v1);

    auto errors = ConfigValidator::validate(config);
    ASSERT_EQ(errors.size(), 1);
    EXPECT_NE(errors[0].find("out of range"), std::string::npos);
}

// --- Stock Key Binding Tests ---

TEST_F(ConfigModuleTest, ParseStockKeys) {
    nlohmann::json j = {
        {"location", {{"address", "London, UK"}, {"lat", 51.5}, {"lon", -0.1}}},
        {"stocks", {{{"symbol", "AAPL"}, {"name", "Apple"}, {"currency_symbol", "$"}}}},
        {"stock_keys", {
            {"next_stock", "dot"},
            {"prev_stock", "comma"},
            {"next_chart", "equal"},
            {"prev_chart", "minus"}
        }},
        {"videos", nlohmann::json::array()}
    };
    std::ofstream out(test_file_);
    out << j.dump();
    out.close();

    ConfigModule config;
    auto result = config.load_or_create_config(test_file_);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->stock_keys.next_stock.has_value());
    EXPECT_TRUE(result->stock_keys.prev_stock.has_value());
    EXPECT_TRUE(result->stock_keys.next_chart.has_value());
    EXPECT_TRUE(result->stock_keys.prev_chart.has_value());
    // All four keys should be distinct
    EXPECT_NE(*result->stock_keys.next_stock, *result->stock_keys.prev_stock);
    EXPECT_NE(*result->stock_keys.next_chart, *result->stock_keys.prev_chart);
}

TEST(ConfigValidatorTest, StockKeyDuplicateWithGlobal) {
    AppConfig config;
    config.location = {"Test", 0.0f, 0.0f};
    config.stocks.push_back({"AAPL", "Apple", "$"});
    config.global_keys.hide_videos = 47; // KEY_V
    config.stock_keys.next_stock = 47;   // Duplicate with global!

    auto errors = ConfigValidator::validate(config);
    ASSERT_GE(errors.size(), 1u);
    EXPECT_NE(errors[0].find("Duplicate key binding"), std::string::npos);
}

TEST(ConfigValidatorTest, StockKeyDuplicateAmongStockKeys) {
    AppConfig config;
    config.location = {"Test", 0.0f, 0.0f};
    config.stocks.push_back({"AAPL", "Apple", "$"});
    config.stock_keys.next_stock = 52;  // KEY_DOT
    config.stock_keys.prev_stock = 52;  // Same as next_stock!

    auto errors = ConfigValidator::validate(config);
    ASSERT_GE(errors.size(), 1u);
    EXPECT_NE(errors[0].find("Duplicate key binding"), std::string::npos);
}

TEST(StockModuleTest, ManualCyclingLogic) {
    StockModule module;
    // Add 3 stocks
    module.add_symbol("AAPL", "Apple", "$");
    module.add_symbol("GOOG", "Alphabet", "$");
    module.add_symbol("MSFT", "Microsoft", "$");

    // next_stock / prev_stock should not crash even with no data
    module.next_stock();
    module.prev_stock();
    module.next_chart();
    module.prev_chart();

    // Test passes if no crash — cycling on empty data is gracefully handled
    SUCCEED();
}
