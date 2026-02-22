#include "renderer.hpp"
#include <iostream>
#include <vector>
#include <cmath>

namespace nuc_display::core {

const char* vertex_shader_source = R"(
    attribute vec4 a_position;
    attribute vec2 a_texCoord;
    varying vec2 v_texCoord;
    uniform mat4 u_matrix;
    void main() {
        gl_Position = u_matrix * a_position;
        v_texCoord = a_texCoord;
    }
)";

const char* fragment_shader_source = R"(
    precision mediump float;
    varying vec2 v_texCoord;
    uniform sampler2D s_texture;
    uniform vec4 u_color;
    uniform int u_type; // 0 for icon (RGBA), 1 for text (Luminance as Alpha)
    void main() {
        vec4 texel = texture2D(s_texture, v_texCoord);
        if (u_type == 1) {
            gl_FragColor = vec4(u_color.rgb, u_color.a * texel.r);
        } else {
            gl_FragColor = u_color * texel;
        }
    }
)";

Renderer::Renderer() : program_(0), position_loc_(0), tex_coord_loc_(0), sampler_loc_(0), matrix_loc_(0), vbo_(0), width_(0), height_(0) {
    for (int i = 0; i < 16; i++) matrix_[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

Renderer::~Renderer() {
    if (program_) glDeleteProgram(program_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
}

void Renderer::init(int width, int height) {
    this->width_ = width;
    this->height_ = height;

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
    program_ = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    position_loc_ = glGetAttribLocation(program_, "a_position");
    tex_coord_loc_ = glGetAttribLocation(program_, "a_texCoord");
    sampler_loc_ = glGetUniformLocation(program_, "s_texture");
    matrix_loc_ = glGetUniformLocation(program_, "u_matrix");
    color_loc_  = glGetUniformLocation(program_, "u_color");

    glGenBuffers(1, &vbo_);
    
    // Create a 1x1 white texture for untextured solid drawing
    uint8_t white_pixel[4] = {255, 255, 255, 255};
    white_texture_ = create_texture(white_pixel, 1, 1, 4);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    update_matrix();
}

void Renderer::update_matrix() {
    // Identity
    for (int i = 0; i < 16; i++) matrix_[i] = (i % 5 == 0) ? 1.0f : 0.0f;

    // Flip vertical (standard GLES2 is y-up, we want y-down for UI)
    // Map [0, 1] to [-1, 1] with y-down: y' = 1.0 - 2.0 * y
    // Matrix for [0, 1] space to NDC:
    // x' = x * 2 - 1
    // y' = 1 - y * 2
    
    // Initial mapping from 0..1 to NDC
    matrix_[0] = 2.0f;  matrix_[12] = -1.0f;
    matrix_[5] = -2.0f; matrix_[13] = 1.0f;

    // Apply Flip
    if (flip_h_) { matrix_[0] *= -1.0f; matrix_[12] *= -1.0f; }
    if (flip_v_) { matrix_[5] *= -1.0f; matrix_[13] *= -1.0f; }

    // Apply Rotation (simplified for 0, 90, 180, 270)
    if (rotation_ != 0) {
        float rad = rotation_ * 3.14159f / 180.0f;
        float c = cos(rad);
        float s = sin(rad);
        float m0 = matrix_[0], m12 = matrix_[12];
        float m5 = matrix_[5], m13 = matrix_[13];
        
        matrix_[0] = m0 * c;
        matrix_[1] = m0 * s;
        matrix_[4] = m5 * -s;
        matrix_[5] = m5 * c;
        matrix_[12] = m12 * c - m13 * s;
        matrix_[13] = m12 * s + m13 * c;
    }
}

void Renderer::set_rotation(int degrees) {
    rotation_ = degrees;
    update_matrix();
}

void Renderer::set_flip(bool horizontal, bool vertical) {
    flip_h_ = horizontal;
    flip_v_ = vertical;
    update_matrix();
}

void Renderer::clear(float r, float g, float b, float a) {
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

uint32_t Renderer::create_texture(const uint8_t* data, int width, int height, int channels) {
    GLuint texture_id;
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    GLenum format = (channels == 4) ? GL_RGBA : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    return texture_id;
}

void Renderer::delete_texture(uint32_t texture_id) {
    GLuint tid = texture_id;
    glDeleteTextures(1, &tid);
}

void Renderer::draw_quad(uint32_t texture_id, float x, float y, float w, float h, float r, float g, float b, float a) {
    glUseProgram(program_);

    float vertices[] = {
        x,     y,     0.0f, 0.0f,
        x + w, y,     1.0f, 0.0f,
        x,     y + h, 0.0f, 1.0f,
        x + w, y + h, 1.0f, 1.0f,
    };

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

    glUniformMatrix4fv(matrix_loc_, 1, GL_FALSE, matrix_);
    glUniform4f(color_loc_, r, g, b, a);
    glUniform1i(glGetUniformLocation(program_, "u_type"), 0);

    glVertexAttribPointer(position_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(position_loc_);

    glVertexAttribPointer(tex_coord_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(tex_coord_loc_);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glUniform1i(sampler_loc_, 0);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Renderer::draw_text(const std::vector<modules::GlyphData>& glyphs, float start_x, float start_y, float scale, float r, float g, float b, float a) {
    glUseProgram(program_);
    glUniform1i(glGetUniformLocation(program_, "u_type"), 1);
    glUniform4f(color_loc_, r, g, b, a);
    glUniformMatrix4fv(matrix_loc_, 1, GL_FALSE, matrix_);

    float x = start_x;
    for (const auto& glyph : glyphs) {
        if (glyph.texture_id == 0) continue;

        float w = (float)glyph.width / width_ * scale;
        float h = (float)glyph.height / height_ * scale;
        
        float xpos = x + (float)glyph.bearing_x / width_ * scale;
        float ypos = start_y - (float)glyph.bearing_y / height_ * scale;

        // Custom quad draw for text to avoid changing uniforms constantly
        float vertices[] = {
            xpos,     ypos,     0.0f, 0.0f,
            xpos + w, ypos,     1.0f, 0.0f,
            xpos,     ypos + h, 0.0f, 1.0f,
            xpos + w, ypos + h, 1.0f, 1.0f,
        };

        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

        glVertexAttribPointer(position_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(position_loc_);

        glVertexAttribPointer(tex_coord_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(tex_coord_loc_);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, glyph.texture_id);
        glUniform1i(sampler_loc_, 0);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        x += (float)glyph.advance / width_ * scale;
    }
}

void Renderer::draw_line_strip(const std::vector<float>& points, float r, float g, float b, float a, float line_width) {
    if (points.size() < 4) return; // Need at least two x,y pairs

    glUseProgram(program_);
    glLineWidth(line_width);

    std::vector<float> vertices;
    vertices.reserve(points.size() * 2);
    for (size_t i = 0; i < points.size(); i += 2) {
        vertices.push_back(points[i]);
        vertices.push_back(points[i+1]);
        vertices.push_back(0.0f); // Default texCoord
        vertices.push_back(0.0f);
    }

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_DYNAMIC_DRAW);

    glUniformMatrix4fv(matrix_loc_, 1, GL_FALSE, matrix_);
    glUniform4f(color_loc_, r, g, b, a);
    glUniform1i(glGetUniformLocation(program_, "u_type"), 0);

    glVertexAttribPointer(position_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(position_loc_);

    glVertexAttribPointer(tex_coord_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(tex_coord_loc_);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, white_texture_);
    glUniform1i(sampler_loc_, 0);

    glDrawArrays(GL_LINE_STRIP, 0, points.size() / 2);
}

GLuint Renderer::compile_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint infoLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 1) {
            std::vector<char> infoLog(infoLen);
            glGetShaderInfoLog(shader, infoLen, nullptr, infoLog.data());
            std::cerr << "Error compiling shader:\n" << infoLog.data() << "\n";
        }
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint Renderer::link_program(GLuint vertex_shader, GLuint fragment_shader) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint infoLen = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 1) {
            std::vector<char> infoLog(infoLen);
            glGetProgramInfoLog(program, infoLen, nullptr, infoLog.data());
            std::cerr << "Error linking program:\n" << infoLog.data() << "\n";
        }
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

} // namespace nuc_display::core
