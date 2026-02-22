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

    // Pseudo-random hash
    float hash(vec2 p) {
        return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
    }
    
    // Value noise
    float noise(vec2 p) {
        vec2 i = floor(p);
        vec2 f = fract(p);
        vec2 u = f*f*(3.0-2.0*f);
        return mix(mix(hash(i + vec2(0.0,0.0)), hash(i + vec2(1.0,0.0)), u.x),
                   mix(hash(i + vec2(0.0,1.0)), hash(i + vec2(1.0,1.0)), u.x), u.y);
    }
    
    // Fractional Brownian Motion (fBM)
    float fbm(vec2 p) {
        float f = 0.0;
        float amp = 0.5;
        vec2 shift = vec2(100.0);
        mat2 rot = mat2(cos(0.5), sin(0.5), -sin(0.5), cos(0.50));
        for (int i = 0; i < 5; i++) {
            f += amp * noise(p);
            p = rot * p * 2.0 + shift;
            amp *= 0.5;
        }
        return f;
    }

    void main() {
        // Coordinate mapping from (-1, -1) to (1, 1)
        vec2 uv = v_texCoord * 2.0 - 1.0;
        uv.y *= -1.0; // Invert Y for standard math orientation

        // Determine Weather Type: 0=Clear, 1=Cloudy, 2=Rain, 3=Snow, 4=Storm
        int type = 0;
        if (u_weather_code >= 1 && u_weather_code <= 3) type = 1;
        if (u_weather_code >= 51 && u_weather_code <= 67) type = 2; // Rain
        if (u_weather_code >= 71 && u_weather_code <= 86) type = 3; // Snow
        if (u_weather_code >= 95) type = 4; // Storm
        
        vec3 col = vec3(0.0);
        float alpha = 0.0;
        
        // Scale uv to zoom in the objects slightly
        vec2 sc_uv = uv * 0.65;
        
        // --- Background Stars (Night & Clear/Partly Cloudy only) ---
        if (u_is_night == 1 && type <= 1) {
            float star_hash = hash(floor(uv * 15.0));
            if (star_hash > 0.95) {
                // Twinkle effect
                float twinkle = 0.5 + 0.5 * sin(u_time * 3.0 + star_hash * 10.0);
                float star_size = hash(floor(uv * 15.0) + vec2(1.0)) * 0.02;
                vec2 star_uv = fract(uv * 15.0) - 0.5;
                float star_dist = length(star_uv);
                float star = smoothstep(star_size, 0.0, star_dist) * twinkle;
                if (star > 0.0) {
                    col += vec3(0.9, 0.9, 1.0) * star;
                    alpha = max(alpha, star * 0.8);
                }
            }
        }
        
        // --- Celestial Body (Sun or Moon) ---
        vec2 body_pos = vec2(0.0, 0.15); // Centered horizontally, slightly up
        if (type < 4) { // Don't draw in heavy storms
            if (u_is_night == 1) {
                // Draw Crescent Moon
                float moon_dist = length(uv - body_pos);
                float shadow_dist = length(uv - (body_pos + vec2(0.08, 0.05))); // Shifted cut-out
                float moon_glow = exp(-moon_dist * 5.0) * 0.4;
                float moon_core = smoothstep(0.25, 0.22, moon_dist) - smoothstep(0.25, 0.22, shadow_dist);
                moon_core = clamp(moon_core, 0.0, 1.0);
                
                vec3 moonCol = vec3(0.9, 0.95, 1.0);
                col += moonCol * moon_core + vec3(0.3, 0.4, 0.6) * moon_glow;
                alpha = max(alpha, moon_core + moon_glow);
            } else {
                // Draw Bright Sun
                float sun_dist = length(uv - body_pos);
                float sunIntensity = exp(-sun_dist * 4.0);
                // Core sun
                float sun_core = smoothstep(0.2, 0.15, sun_dist);
                col += vec3(1.0, 0.9, 0.4) * sun_core + vec3(1.0, 0.6, 0.1) * sunIntensity * 0.8;
                alpha = max(alpha, max(sun_core, sunIntensity));
            }
        }

        // --- Animated Volumetric Cloud Layer ---
        float cloudCover = 0.0;
        if (type > 0) {
            vec2 cloudUV = uv * 2.0;
            float speed = (type >= 2) ? 0.3 : 0.1;
            if (type == 4) speed = 0.8;
            cloudUV.x += u_time * speed;
            
            float density = fbm(cloudUV);
            
            // Mask to keep clouds in the upper half generally
            float mask = 1.0 - smoothstep(0.0, 1.5, length(uv - vec2(0.0, 0.5)));
            density *= mask;
            density = smoothstep(0.3, 0.7, density);
            
            vec2 lightDir = normalize(vec2(0.5, 0.5));
            float dX = fbm(cloudUV + vec2(0.01, 0.0)) - fbm(cloudUV - vec2(0.01, 0.0));
            float dY = fbm(cloudUV + vec2(0.0, 0.01)) - fbm(cloudUV - vec2(0.0, 0.01));
            vec3 normal = normalize(vec3(-dX, -dY, 1.0));
            vec3 lightVec = normalize(vec3(lightDir, 1.0));
            
            float diffuse = max(dot(normal, lightVec), 0.0);
            
            vec3 baseCloudCol = vec3(1.0);
            if (u_is_night == 1) {
                baseCloudCol = vec3(0.3, 0.3, 0.4); // Dark blueish clouds at night
                if (type >= 2) baseCloudCol = vec3(0.15, 0.15, 0.2); // Darker storm clouds
            } else {
                if (type >= 2) baseCloudCol = vec3(0.55, 0.6, 0.65);
                if (type == 4) baseCloudCol = vec3(0.35, 0.4, 0.45);
            }
            
            // Lightning for storms
            if (type == 4) {
                float flash = sin(u_time * 15.0) * sin(u_time * 2.0);
                if (flash > 0.8) baseCloudCol += vec3(0.6, 0.7, 1.0);
            }
            
            vec3 finalCloudCol = baseCloudCol * (0.3 + 0.7 * diffuse);
            
            cloudCover = density;
            // Mix cloud over existing background (sun/moon)
            col = mix(col, finalCloudCol, cloudCover);
            alpha = max(alpha, cloudCover);
        }
        
        // --- Precipitation Layer (Rain or Snow) ---
        if (type == 2 || type == 3 || type == 4) {
            vec2 p_uv = uv;
            float fallSpeed = (type == 3) ? 0.4 : 2.0;
            if (type == 4) fallSpeed = 3.5;
            
            // Dynamic rain/snow rendering
            if (type == 3) {
                // Elegant Snowflakes
                p_uv.y += u_time * fallSpeed;
                p_uv.x += sin(u_time + p_uv.y * 5.0) * 0.1; // Swaying motion
                
                vec2 p_id = floor(p_uv * 10.0);
                vec2 p_f = fract(p_uv * 10.0) - 0.5;
                float p_hash = hash(p_id);
                
                if (p_hash > 0.6) {
                    float flake = smoothstep(0.15, 0.02, length(p_f - vec2(0.0, p_hash - 0.5)));
                    col = mix(col, vec3(1.0), flake * 0.8);
                    alpha = max(alpha, flake * 0.8);
                }
            } else {
                // Realistic Rain (Multiple skewed layers at varying speeds)
                float rain_intensity = 0.0;
                // Layer 1 (background, slower, thinner)
                vec2 r1_uv = uv * vec2(20.0, 5.0);
                r1_uv.y += u_time * fallSpeed * 4.0;
                r1_uv.x += uv.y * 2.0; // slight slant
                float n1 = hash(floor(r1_uv));
                if (n1 > 0.85) {
                    vec2 r1_f = fract(r1_uv) - 0.5;
                    float drop1 = smoothstep(0.1, 0.0, abs(r1_f.x)) * smoothstep(0.5, 0.0, abs(r1_f.y + n1 - 0.5));
                    rain_intensity += drop1 * 0.3;
                }
                
                // Layer 2 (foreground, faster, thicker)
                vec2 r2_uv = uv * vec2(10.0, 3.0);
                r2_uv.y += u_time * fallSpeed * 6.0;
                r2_uv.x += uv.y * 3.0; // steeper slant
                if (type == 4) r2_uv.x += uv.y * 6.0 + u_time * 2.0; // heavy wind in storm
                float n2 = hash(floor(r2_uv));
                if (n2 > 0.75) {
                    vec2 r2_f = fract(r2_uv) - 0.5;
                    float drop2 = smoothstep(0.15, 0.0, abs(r2_f.x)) * smoothstep(0.4, 0.0, abs(r2_f.y + n2 - 0.5));
                    rain_intensity += drop2 * 0.6;
                }
                
                vec3 rain_col = (u_is_night == 1) ? vec3(0.6, 0.7, 0.9) : vec3(0.8, 0.9, 1.0);
                col = mix(col, rain_col, min(rain_intensity, 1.0));
                alpha = max(alpha, min(rain_intensity, 1.0));
            }
        }
        
        gl_FragColor = vec4(col, clamp(alpha, 0.0, 1.0));
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
