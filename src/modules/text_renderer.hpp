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

#include "core/renderer.hpp"

namespace nuc_display::modules {

class TextRenderer : public MediaModule {
public:
    TextRenderer();
    ~TextRenderer() override;

    std::expected<void, MediaError> load(const std::string& font_filepath) override;
    std::expected<void, MediaError> process(double time_sec) override;

    std::expected<void, MediaError> set_pixel_size(uint32_t width, uint32_t height);
    std::expected<std::vector<GlyphData>, MediaError> shape_text(const std::string& utf8_text);
    
    // GLES2 helpers
    void clear_cache();

private:
    FT_Library ft_library_ = nullptr;
    FT_Face ft_face_ = nullptr;
    hb_font_t* hb_font_ = nullptr;
    
    struct CachedGlyph {
        unsigned int texture_id;
        int width, height;
        int bearing_x, bearing_y;
        long advance;
    };
    std::vector<CachedGlyph> glyph_cache_; // Simple indexed cache for ASCII/common chars or a map
    // For now we'll use a map or simple logic
    std::vector<GlyphData> cached_glyphs_;
};

} // namespace nuc_display::modules
