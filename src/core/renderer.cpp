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

const char* weather_fragment_shader = R"(
    precision mediump float;
    varying vec2 v_texCoord;
    uniform float u_time;
    uniform int u_weather_code;
    uniform int u_is_night;

    // --- SDF Helpers ---
    float sdCircle(vec2 p, float r) { return length(p) - r; }
    float sdCapsule(vec2 p, vec2 a, vec2 b, float r) {
        vec2 pa = p - a, ba = b - a;
        float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
        return length(pa - ba * h) - r;
    }
    
    // Polygon SDF for Lightning
    float sdLightning(vec2 p) {
        float d = sdCapsule(p, vec2(0.0, 0.2), vec2(0.1, -0.1), 0.05);
        d = min(d, sdCapsule(p, vec2(0.1, -0.1), vec2(-0.05, -0.05), 0.05));
        d = min(d, sdCapsule(p, vec2(-0.05, -0.05), vec2(0.05, -0.4), 0.03));
        return d;
    }

    // Cloud SDF (Union of 3 circles and a flat bottom)
    float sdCloud(vec2 p) {
        float d = sdCircle(p - vec2(0.0, 0.1), 0.35); // Main center
        d = min(d, sdCircle(p - vec2(-0.35, -0.05), 0.25)); // Left
        d = min(d, sdCircle(p - vec2(0.35, -0.05), 0.25)); // Right
        // Flatten bottom
        d = max(d, -(p.y + 0.15));
        d = min(d, sdCapsule(p, vec2(-0.35, -0.15), vec2(0.35, -0.15), 0.1));
        return d;
    }

    void main() {
        vec2 uv = v_texCoord * 2.0 - 1.0;
        uv.y *= -1.0; 
        
        float blur = 0.015;
        
        int type = 0;
        if (u_weather_code >= 1 && u_weather_code <= 3) type = 1; // Partly cloudy
        if (u_weather_code == 45 || u_weather_code == 48) type = 1; // Fog -> treat as cloudy
        if (u_weather_code >= 51 && u_weather_code <= 67) type = 2; // Rain
        if (u_weather_code >= 71 && u_weather_code <= 86) type = 3; // Snow
        if (u_weather_code >= 95) type = 4; // Storm
        
        vec3 col = vec3(0.0);
        float final_alpha = 0.0;
        
        // --- Layer 1: Sun / Moon ---
        float sun_dist = 100.0;
        float moon_dist = 100.0;
        float corona_dist = 100.0;
        vec3 body_col = vec3(0.0);
        
        vec2 body_pos = (type == 0) ? vec2(0.0, 0.0) : vec2(0.35, 0.35);
        
        if (type < 4) { // No sun/moon in storms
            if (u_is_night == 1) {
                float d1 = sdCircle(uv - body_pos, 0.35);
                float d2 = sdCircle(uv - (body_pos + vec2(0.12, 0.08)), 0.3);
                moon_dist = max(d1, -d2);
                body_col = vec3(0.9, 0.95, 1.0);
            } else {
                sun_dist = sdCircle(uv - body_pos, 0.35);
                body_col = vec3(1.0, 0.75, 0.1); // Golden Yellow
                float pulse = 1.0 + 0.05 * sin(u_time * 2.0);
                corona_dist = sdCircle(uv - body_pos, 0.35 * pulse);
            }
        }
        
        // Render Sun/Moon
        if (u_is_night == 0 && type < 4) {
            float sun_alpha = 1.0 - smoothstep(0.0, blur, sun_dist);
            float corona_alpha = (1.0 - smoothstep(0.0, 0.3, corona_dist)) * 0.4;
            
            vec3 sun_final = mix(vec3(1.0, 0.9, 0.2), body_col, sun_alpha); // Bright center
            
            col = mix(col, vec3(1.0, 0.6, 0.0), corona_alpha); // Orange glow
            final_alpha = max(final_alpha, corona_alpha);
            
            col = mix(col, sun_final, sun_alpha);
            final_alpha = max(final_alpha, sun_alpha);
        } else if (u_is_night == 1 && type < 4) {
            float moon_alpha = 1.0 - smoothstep(0.0, blur, moon_dist);
            col = mix(col, body_col, moon_alpha);
            final_alpha = max(final_alpha, moon_alpha);
            
            float glow_alpha = (1.0 - smoothstep(0.0, 0.4, sdCircle(uv - body_pos, 0.35))) * 0.3;
            col = mix(col, vec3(0.4, 0.6, 1.0), glow_alpha * (1.0 - moon_alpha));
            final_alpha = max(final_alpha, glow_alpha);
        }
        
        // --- Layer 2: Background Cloud ---
        float bcloud_alpha = 0.0;
        if (type > 0) {
            vec2 c_uv = uv - vec2(0.2 * sin(u_time * 0.4) - 0.2, 0.1); // Slow parallax 
            float cloud_dist = sdCloud(c_uv * 1.2); // scaled down slightly
            
            float shadow = 1.0 - smoothstep(0.0, 0.2, cloud_dist - 0.1);
            
            bcloud_alpha = 1.0 - smoothstep(0.0, blur, cloud_dist);
            vec3 c_col = (u_is_night == 1) ? vec3(0.35, 0.4, 0.5) : vec3(0.8, 0.85, 0.9);
            if (type >= 2) c_col = (u_is_night == 1) ? vec3(0.2, 0.25, 0.3) : vec3(0.5, 0.55, 0.6); // Darker for rain/storm
            
            col = mix(col, vec3(0.0), shadow * 0.3 * (1.0 - bcloud_alpha));
            final_alpha = max(final_alpha, shadow * 0.3);
            
            col = mix(col, c_col, bcloud_alpha);
            final_alpha = max(final_alpha, bcloud_alpha);
        }
        
        // --- Layer 3: Rain / Snow / Lightning ---
        if (type >= 2) {
            vec2 p_uv = uv;
            vec3 p_col = vec3(1.0);
            
            if (type == 4) {
                float flash_time = fract(u_time * 0.5);
                if (flash_time > 0.8) {
                    float l_dist = sdLightning(uv - vec2(0.0, -0.2));
                    float l_alpha = 1.0 - smoothstep(0.0, blur, l_dist);
                    float l_glow = (1.0 - smoothstep(0.0, 0.3, l_dist)) * 0.6;
                    
                    p_col = vec3(1.0, 0.9, 0.3); // Yellow lightning
                    col = mix(col, p_col, l_glow * sin(u_time * 30.0)); // strobe
                    final_alpha = max(final_alpha, l_glow);
                    col = mix(col, vec3(1.0), l_alpha * sin(u_time * 30.0));
                    final_alpha = max(final_alpha, l_alpha);
                }
            }
            
            float fallSpeed = (type == 3) ? 0.3 : 1.5;
            if (type == 4) fallSpeed = 2.5;
            
            p_uv.y += u_time * fallSpeed;
            if (type == 3) p_uv.x += sin(u_time * 2.0 + p_uv.y * 3.0) * 0.1; // snow sway
            
            vec2 id = floor(p_uv * 4.0);
            vec2 f = fract(p_uv * 4.0) - 0.5;
            
            float r = fract(sin(dot(id, vec2(12.9898, 78.233))) * 43758.5453);
            
            if (r > 0.4) {
                vec2 offset = vec2(r * 0.6 - 0.3, r * 0.8 - 0.4);
                float dist;
                
                if (type == 3) { // Snow
                    dist = sdCircle(f - offset, 0.06);
                    p_col = vec3(1.0);
                } else { // Rain
                    float slant = (type == 4) ? 0.15 : 0.05;
                    dist = sdCapsule(f - offset, vec2(slant, 0.15), vec2(-slant, -0.15), 0.02);
                    p_col = vec3(0.5, 0.7, 1.0);
                }
                
                float a = 1.0 - smoothstep(0.0, blur, dist);
                float mask = smoothstep(-0.2, -0.1, uv.y);
                a *= mask;
                
                col = mix(col, p_col, a);
                final_alpha = max(final_alpha, a);
            }
        }
        
        // --- Layer 4: Foreground Cloud ---
        if (type > 0 || u_weather_code == 0) { 
            if (type > 0) {
                vec2 c_uv = uv - vec2(-0.1 * sin(u_time * 0.6) + 0.1, -0.15); // Parallax offset
                float cloud_dist = sdCloud(c_uv * 1.0);
                
                float shadow = 1.0 - smoothstep(0.0, 0.25, cloud_dist - 0.1);
                float fcloud_alpha = 1.0 - smoothstep(0.0, blur, cloud_dist);
                
                vec3 c_col = (u_is_night == 1) ? vec3(0.45, 0.5, 0.6) : vec3(1.0); 
                if (type >= 2) c_col = (u_is_night == 1) ? vec3(0.25, 0.3, 0.35) : vec3(0.65, 0.7, 0.75); // Darker for rain/storm
                
                col = mix(col, vec3(0.0), shadow * 0.4 * (1.0 - fcloud_alpha));
                final_alpha = max(final_alpha, shadow * 0.4);
                
                col = mix(col, c_col, fcloud_alpha);
                final_alpha = max(final_alpha, fcloud_alpha);
            }
        }
        
        gl_FragColor = vec4(col, clamp(final_alpha, 0.0, 1.0));
    }
)";

Renderer::Renderer() : program_(0), position_loc_(0), tex_coord_loc_(0), sampler_loc_(0), matrix_loc_(0), color_loc_(0), weather_program_(0), weather_pos_loc_(0), weather_matrix_loc_(0), weather_time_loc_(0), weather_code_loc_(0), weather_coord_loc_(0), vbo_(0), white_texture_(0), width_(0), height_(0) {
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
    type_loc_   = glGetUniformLocation(program_, "u_type");

    // Weather Shader initialization
    GLuint vs_w = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fs_weather = compile_shader(GL_FRAGMENT_SHADER, weather_fragment_shader);
    weather_program_ = link_program(vs_w, fs_weather);
    glDeleteShader(vs_w);
    glDeleteShader(fs_weather);

    weather_pos_loc_ = glGetAttribLocation(weather_program_, "a_position");
    weather_coord_loc_ = glGetAttribLocation(weather_program_, "a_texCoord");
    weather_matrix_loc_ = glGetUniformLocation(weather_program_, "u_matrix");
    weather_time_loc_ = glGetUniformLocation(weather_program_, "u_time");
    weather_code_loc_ = glGetUniformLocation(weather_program_, "u_weather_code");
    weather_is_night_loc_ = glGetUniformLocation(weather_program_, "u_is_night");

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
    glUniform1i(type_loc_, 0);

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
    if (glyphs.empty()) return;

    glUseProgram(program_);
    glUniform1i(type_loc_, 1);
    glUniform4f(color_loc_, r, g, b, a);
    glUniformMatrix4fv(matrix_loc_, 1, GL_FALSE, matrix_);
    glUniform1i(sampler_loc_, 0);
    glActiveTexture(GL_TEXTURE0);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glVertexAttribPointer(position_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(position_loc_);
    glVertexAttribPointer(tex_coord_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(tex_coord_loc_);

    // Batch: group consecutive glyphs sharing the same texture into a single draw call.
    // Most text uses one texture per glyph, so we issue one draw per glyph but with
    // minimal state changes (uniforms/attribs set once above, only texture bind changes).
    float x = start_x;
    GLuint last_tex = 0;

    for (const auto& glyph : glyphs) {
        if (glyph.texture_id == 0) {
            x += glyph.advance / (float)width_ * scale;
            continue;
        }

        float w = (float)glyph.width / width_ * scale;
        float h = (float)glyph.height / height_ * scale;
        float xpos = x + (float)glyph.bearing_x / width_ * scale;
        float ypos = start_y - (float)glyph.bearing_y / height_ * scale;

        float vertices[] = {
            xpos,     ypos,     0.0f, 0.0f,
            xpos + w, ypos,     1.0f, 0.0f,
            xpos,     ypos + h, 0.0f, 1.0f,
            xpos + w, ypos + h, 1.0f, 1.0f,
        };

        // Only bind texture if it changed
        if (glyph.texture_id != last_tex) {
            glBindTexture(GL_TEXTURE_2D, glyph.texture_id);
            last_tex = glyph.texture_id;
        }

        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        x += glyph.advance / (float)width_ * scale;
    }
}

void Renderer::draw_line_strip(const float* points, size_t count, float r, float g, float b, float a, float line_width) {
    if (count < 4) return;
    size_t num_points = count / 2;
    // Stack-alloc interleaved vertices (max 512 points = 2048 floats, ~8KB on stack)
    constexpr size_t MAX_POINTS = 512;
    if (num_points > MAX_POINTS) num_points = MAX_POINTS;
    float vertices[MAX_POINTS * 4];
    for (size_t i = 0; i < num_points; ++i) {
        vertices[i * 4 + 0] = points[i * 2];
        vertices[i * 4 + 1] = points[i * 2 + 1];
        vertices[i * 4 + 2] = 0.0f;
        vertices[i * 4 + 3] = 0.0f;
    }

    glUseProgram(program_);
    glLineWidth(line_width);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, num_points * 4 * sizeof(float), vertices, GL_DYNAMIC_DRAW);

    glUniformMatrix4fv(matrix_loc_, 1, GL_FALSE, matrix_);
    glUniform4f(color_loc_, r, g, b, a);
    glUniform1i(type_loc_, 0);

    glVertexAttribPointer(position_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(position_loc_);

    glVertexAttribPointer(tex_coord_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(tex_coord_loc_);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, white_texture_);
    glUniform1i(sampler_loc_, 0);

    glDrawArrays(GL_LINE_STRIP, 0, num_points);
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

void Renderer::draw_animated_weather(int weather_code, float x, float y, float w, float h, float time_sec, bool is_night) {
    glUseProgram(weather_program_);

    float vertices[] = {
        x,     y,     0.0f, 0.0f,
        x + w, y,     1.0f, 0.0f,
        x,     y + h, 0.0f, 1.0f,
        x + w, y + h, 1.0f, 1.0f,
    };

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

    glUniformMatrix4fv(weather_matrix_loc_, 1, GL_FALSE, matrix_);
    glUniform1f(weather_time_loc_, time_sec);
    glUniform1i(weather_code_loc_, weather_code);
    glUniform1i(weather_is_night_loc_, is_night ? 1 : 0);

    glVertexAttribPointer(weather_pos_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(weather_pos_loc_);

    glVertexAttribPointer(weather_coord_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(weather_coord_loc_);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

} // namespace nuc_display::core
