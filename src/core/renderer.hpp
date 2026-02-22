#pragma once

#include <GLES2/gl2.h>
#include <vector>
#include <cstdint>

namespace nuc_display::modules {
struct GlyphData {
    uint32_t codepoint;
    float x_offset;
    float y_offset;
    float advance;
    unsigned int texture_id;
    int width, height;
    int bearing_x, bearing_y;
};
}

namespace nuc_display::core {

class Renderer {
public:
    Renderer();
    ~Renderer();

    void init(int width, int height);
    void clear(float r, float g, float b, float a);
    
    int width() const { return width_; }
    int height() const { return height_; }
    GLuint vbo() const { return vbo_; }

    
    // Orientation correction
    void set_rotation(int degrees); // 0, 90, 180, 270
    void set_flip(bool horizontal, bool vertical);

    // Texture Helpers
    uint32_t create_texture(const uint8_t* data, int width, int height, int channels);
    void delete_texture(uint32_t texture_id);
    
    // Draw calls (using normalized coords 0.0 to 1.0)
    void draw_quad(uint32_t texture_id, float x, float y, float w, float h, float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);
    void draw_text(const std::vector<modules::GlyphData>& glyphs, float start_x, float start_y, float scale, float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);
    void draw_line_strip(const std::vector<float>& points, float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f, float line_width = 2.0f);
    void draw_animated_weather(int weather_code, float x, float y, float w, float h, float time_sec, bool is_night = false);

    GLuint compile_shader(GLenum type, const char* source);
    GLuint link_program(GLuint vertex_shader, GLuint fragment_shader);

private:
    void update_matrix();

    GLuint program_;
    GLuint position_loc_;
    GLuint tex_coord_loc_;
    GLuint sampler_loc_;
    GLuint matrix_loc_;
    GLuint color_loc_;
    
    // Weather Shader
    GLuint weather_program_;
    GLuint weather_pos_loc_;
    GLuint weather_matrix_loc_;
    GLuint weather_time_loc_;
    GLuint weather_code_loc_;
    GLuint weather_is_night_loc_;
    GLuint weather_coord_loc_;

    GLuint vbo_;
    GLuint white_texture_;

    float matrix_[16];
    int width_, height_;
    int rotation_ = 0;
    bool flip_h_ = false;
    bool flip_v_ = false;
};

} // namespace nuc_display::core
