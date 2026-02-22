#include "modules/text_renderer.hpp"
#include <iostream>

namespace nuc_display::modules {

TextRenderer::TextRenderer() {
    if (FT_Init_FreeType(&this->ft_library_)) {
        std::cerr << "Could not init FreeType library\n";
    }
}

TextRenderer::~TextRenderer() {
    this->clear_cache();
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

void TextRenderer::clear_cache() {
    for (auto& glyph : glyph_cache_) {
        if (glyph.texture_id) {
            glDeleteTextures(1, &glyph.texture_id);
        }
    }
    glyph_cache_.clear();
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

    this->clear_cache(); // Ensure new sizes are rendered
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

    // Ensure cache is big enough or use a more efficient map
    // For simplicity, we'll cache by codepoint (HarfBuzz codepoints are often GID)
    
    for (unsigned int i = 0; i < glyph_count; i++) {
        uint32_t gid = glyph_info[i].codepoint;
        
        // Linear search or resize-based cache
        if (gid >= glyph_cache_.size()) {
            glyph_cache_.resize(gid + 1, {0, 0, 0, 0, 0, 0});
        }
        
        if (glyph_cache_[gid].texture_id == 0) {
            // Load and render glyph
            if (FT_Load_Glyph(ft_face_, gid, FT_LOAD_RENDER)) continue;
            
            GLuint tex;
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            
            // WebGL/GLES2 doesn't support GL_RED necessarily, use GL_LUMINANCE for monochrome
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 
                         ft_face_->glyph->bitmap.width, 
                         ft_face_->glyph->bitmap.rows, 
                         0, GL_LUMINANCE, GL_UNSIGNED_BYTE, 
                         ft_face_->glyph->bitmap.buffer);
            
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            
            glyph_cache_[gid] = {
                .texture_id = tex,
                .width     = (int)ft_face_->glyph->bitmap.width,
                .height    = (int)ft_face_->glyph->bitmap.rows,
                .bearing_x = ft_face_->glyph->bitmap_left,
                .bearing_y = ft_face_->glyph->bitmap_top,
                .advance   = ft_face_->glyph->advance.x
            };
        }

        auto& cached = glyph_cache_[gid];
        layout.push_back({
            .codepoint = gid,
            .x_offset  = glyph_pos[i].x_offset / 64.0f,
            .y_offset  = glyph_pos[i].y_offset / 64.0f,
            .advance   = glyph_pos[i].x_advance / 64.0f,
            .texture_id = cached.texture_id,
            .width     = cached.width,
            .height    = cached.height,
            .bearing_x = cached.bearing_x,
            .bearing_y = cached.bearing_y
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
