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
    if (this->hb_buffer_) {
        hb_buffer_destroy(this->hb_buffer_);
    }
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
    for (auto& [key, glyph] : glyph_cache_) {
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

    // Create persistent HarfBuzz buffer (reused via hb_buffer_reset)
    this->hb_buffer_ = hb_buffer_create();
    if (!this->hb_buffer_ || !hb_buffer_allocation_successful(this->hb_buffer_)) {
        std::cerr << "HarfBuzz: Failed to create buffer\n";
        return std::unexpected(MediaError::InternalError);
    }

    std::cout << "Successfully loaded font: " << font_filepath << "\n";
    return {};
}

std::expected<void, MediaError> TextRenderer::set_pixel_size(uint32_t width, uint32_t height) {
    if (!this->ft_face_ || !this->hb_font_) return std::unexpected(MediaError::InternalError);

    if (current_width_ == width && current_height_ == height) return {};

    if (FT_Set_Pixel_Sizes(this->ft_face_, width, height)) {
        return std::unexpected(MediaError::InternalError);
    }

    // No cache clear! Size-keyed cache retains all previously rendered glyphs.
    hb_ft_font_changed(this->hb_font_);
    current_width_ = width;
    current_height_ = height;
    return {};
}

std::expected<std::vector<GlyphData>, MediaError> TextRenderer::shape_text(const std::string& utf8_text) {
    if (!this->hb_font_ || !this->hb_buffer_) return std::unexpected(MediaError::InternalError);

    // Reuse persistent buffer
    hb_buffer_reset(this->hb_buffer_);
    hb_buffer_add_utf8(this->hb_buffer_, utf8_text.c_str(), -1, 0, -1);
    hb_buffer_guess_segment_properties(this->hb_buffer_);

    hb_shape(this->hb_font_, this->hb_buffer_, nullptr, 0);

    unsigned int glyph_count;
    hb_glyph_info_t* glyph_info = hb_buffer_get_glyph_infos(this->hb_buffer_, &glyph_count);
    hb_glyph_position_t* glyph_pos = hb_buffer_get_glyph_positions(this->hb_buffer_, &glyph_count);

    std::vector<GlyphData> layout;
    layout.reserve(glyph_count);

    // Cache key: (pixel_height << 32) | glyph_id
    uint64_t size_key = static_cast<uint64_t>(current_height_) << 32;

    for (unsigned int i = 0; i < glyph_count; i++) {
        uint32_t gid = glyph_info[i].codepoint;
        uint64_t cache_key = size_key | gid;
        
        auto it = glyph_cache_.find(cache_key);
        if (it == glyph_cache_.end()) {
            // Load and render glyph
            if (FT_Load_Glyph(ft_face_, gid, FT_LOAD_RENDER)) continue;
            
            GLuint tex;
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            
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
            
            CachedGlyph cached = {
                .texture_id = tex,
                .width     = (int)ft_face_->glyph->bitmap.width,
                .height    = (int)ft_face_->glyph->bitmap.rows,
                .bearing_x = ft_face_->glyph->bitmap_left,
                .bearing_y = ft_face_->glyph->bitmap_top,
                .advance   = ft_face_->glyph->advance.x
            };
            it = glyph_cache_.emplace(cache_key, cached).first;
        }

        auto& cached = it->second;
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

    return layout;
}

std::expected<void, MediaError> TextRenderer::process(double /*time_sec*/) {
    if (!this->ft_face_) return std::unexpected(MediaError::InternalError);
    return {};
}

} // namespace nuc_display::modules
