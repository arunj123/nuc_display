#include "modules/text_renderer.hpp"
#include <iostream>

namespace nuc_display::modules {

TextRenderer::TextRenderer() {
    if (FT_Init_FreeType(&this->ft_library_)) {
        std::cerr << "Could not init FreeType library\n";
    }
}

TextRenderer::~TextRenderer() {
    if (this->hb_font_) {
        hb_font_destroy(this->hb_font_);
    }
    if (this->ft_face_) {
        FT_Done_Face(this->ft_face_);
    }
    if (this->ft_library_) {
        FT_Done_FreeType(this->ft_library_);
    }
}

std::expected<void, MediaError> TextRenderer::load(const std::string& font_filepath) {
    if (!this->ft_library_) return std::unexpected(MediaError::InternalError);

    if (FT_New_Face(this->ft_library_, font_filepath.c_str(), 0, &this->ft_face_)) {
        std::cerr << "FreeType: Failed to load font " << font_filepath << "\n";
        return std::unexpected(MediaError::FileNotFound);
    }
    
    FT_Set_Pixel_Sizes(this->ft_face_, 0, 48);

    this->hb_font_ = hb_ft_font_create(this->ft_face_, nullptr);
    if (!this->hb_font_) {
        std::cerr << "HarfBuzz: Failed to create font from FreeType face\n";
        return std::unexpected(MediaError::InternalError);
    }

    std::cout << "Successfully loaded font: " << font_filepath << "\n";
    return {};
}

std::expected<void, MediaError> TextRenderer::set_pixel_size(uint32_t width, uint32_t height) {
    if (!this->ft_face_ || !this->hb_font_) return std::unexpected(MediaError::InternalError);

    if (FT_Set_Pixel_Sizes(this->ft_face_, width, height)) {
        return std::unexpected(MediaError::InternalError);
    }

    hb_ft_font_changed(this->hb_font_);
    return {};
}

std::expected<std::vector<GlyphData>, MediaError> TextRenderer::shape_text(const std::string& utf8_text) {
    if (!this->hb_font_) return std::unexpected(MediaError::InternalError);

    hb_buffer_t* hb_buffer = hb_buffer_create();
    hb_buffer_add_utf8(hb_buffer, utf8_text.c_str(), -1, 0, -1);
    hb_buffer_guess_segment_properties(hb_buffer);

    hb_shape(this->hb_font_, hb_buffer, nullptr, 0);

    unsigned int glyph_count;
    hb_glyph_info_t* glyph_info = hb_buffer_get_glyph_infos(hb_buffer, &glyph_count);
    hb_glyph_position_t* glyph_pos = hb_buffer_get_glyph_positions(hb_buffer, &glyph_count);

    std::vector<GlyphData> layout;
    layout.reserve(glyph_count);

    for (unsigned int i = 0; i < glyph_count; i++) {
        layout.push_back({
            .codepoint = glyph_info[i].codepoint,
            .x_offset  = glyph_pos[i].x_offset / 64.0f,
            .y_offset  = glyph_pos[i].y_offset / 64.0f,
            .advance   = glyph_pos[i].x_advance / 64.0f
        });
    }

    hb_buffer_destroy(hb_buffer);
    return layout;
}

std::expected<void, MediaError> TextRenderer::process(double /*time_sec*/) {
    if (!this->ft_face_) return std::unexpected(MediaError::InternalError);
    return {};
}

} // namespace nuc_display::modules
