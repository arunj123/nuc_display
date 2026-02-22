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
    
    EXPECT_EQ(module.get_weather_description(999), "Unknown");
    EXPECT_EQ(module.get_weather_icon_filename(999), "assets/weather/unknown.png");
}
