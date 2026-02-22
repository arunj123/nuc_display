#pragma once

#include "modules/media_module.hpp"

#include <vector>
#include <string>
#include <memory>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>
#include <expected>

namespace nuc_display::modules {

struct GlyphData {
    uint32_t codepoint;
    float x_offset;
    float y_offset;
    float advance;
};

class TextRenderer : public MediaModule {
public:
    TextRenderer();
    ~TextRenderer() override;

    std::expected<void, MediaError> load(const std::string& font_filepath) override;
    std::expected<void, MediaError> process(double time_sec) override;

    std::expected<void, MediaError> set_pixel_size(uint32_t width, uint32_t height);
    std::expected<std::vector<GlyphData>, MediaError> shape_text(const std::string& utf8_text);

private:
    FT_Library ft_library_ = nullptr;
    FT_Face ft_face_ = nullptr;
    hb_font_t* hb_font_ = nullptr;
};

} // namespace nuc_display::modules
