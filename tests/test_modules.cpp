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
