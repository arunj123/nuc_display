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
    #ifdef GL_FRAGMENT_PRECISION_HIGH
    precision highp float;
    #else
    precision mediump float;
    #endif
    varying vec2 v_texCoord;
    uniform float u_time;
    uniform int u_weather_code;
    uniform int u_is_night;
    uniform float u_wind_speed;

    // --- Helpers ---
    float hash(vec2 p) {
        return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
    }

    float sdCircle(vec2 p, float r) { return length(p) - r; }

    float sdCapsule(vec2 p, vec2 a, vec2 b, float r) {
        vec2 pa = p - a, ba = b - a;
        float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
        return length(pa - ba * h) - r;
    }
    
    // Polygon SDF for Lightning
    float sdLightning(vec2 p) {
        float d = sdCapsule(p, vec2(0.0, 0.2), vec2(0.1, -0.1), 0.04);
        d = min(d, sdCapsule(p, vec2(0.1, -0.1), vec2(-0.05, -0.05), 0.04));
        d = min(d, sdCapsule(p, vec2(-0.05, -0.05), vec2(0.05, -0.4), 0.025));
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
        
        // Parameter mapping
        float rain_intensity = 0.0;
        float snow_intensity = 0.0;
        float hail_intensity = 0.0;
        float fog_density = 0.0;
        float cloud_density = 0.0;
        float storm_intensity = 0.0;
        
        if (u_weather_code == 0) {
            // Clear sky
        } else if (u_weather_code == 1) {
            cloud_density = 0.25;
        } else if (u_weather_code == 2) {
            cloud_density = 0.55;
        } else if (u_weather_code == 3) {
            cloud_density = 1.0;
        } else if (u_weather_code == 45 || u_weather_code == 48) {
            cloud_density = 0.8;
            fog_density = 1.0;
        } else if (u_weather_code >= 51 && u_weather_code <= 55) {
            // Drizzle
            cloud_density = 0.9;
            rain_intensity = 0.12 * float(u_weather_code - 50);
        } else if (u_weather_code == 56 || u_weather_code == 57) {
            // Freezing drizzle
            cloud_density = 0.9;
            rain_intensity = 0.25;
            hail_intensity = 0.2 * float(u_weather_code - 55);
        } else if (u_weather_code >= 61 && u_weather_code <= 65) {
            // Rain
            cloud_density = 1.0;
            rain_intensity = 0.3 + 0.3 * float((u_weather_code - 61) / 2 + 1);
        } else if (u_weather_code == 66 || u_weather_code == 67) {
            // Freezing rain
            cloud_density = 1.0;
            rain_intensity = 0.6;
            hail_intensity = 0.4 * float(u_weather_code - 65);
        } else if (u_weather_code >= 71 && u_weather_code <= 75) {
            // Snow
            cloud_density = 1.0;
            snow_intensity = 0.3 + 0.25 * float((u_weather_code - 71) / 2 + 1);
        } else if (u_weather_code == 77) {
            // Snow grains
            cloud_density = 1.0;
            snow_intensity = 0.4;
            hail_intensity = 0.4;
        } else if (u_weather_code >= 80 && u_weather_code <= 82) {
            // Rain showers
            cloud_density = 1.0;
            rain_intensity = 0.6 + 0.3 * float(u_weather_code - 79);
        } else if (u_weather_code == 85 || u_weather_code == 86) {
            // Snow showers
            cloud_density = 1.0;
            snow_intensity = 0.6 + 0.4 * float(u_weather_code - 84);
        } else if (u_weather_code == 95) {
            // Thunderstorm
            cloud_density = 1.0;
            rain_intensity = 1.0;
            storm_intensity = 0.7;
        } else if (u_weather_code == 96 || u_weather_code == 99) {
            // Thunderstorm with hail
            cloud_density = 1.0;
            rain_intensity = 1.2;
            hail_intensity = 0.8;
            storm_intensity = 1.0;
        }
        
        vec3 col = vec3(0.0);
        float final_alpha = 0.0;
        
        // --- Layer 1: Sun / Moon / Stars / Sky Flashes ---
        float sun_dist = 100.0;
        float moon_dist = 100.0;
        float corona_dist = 100.0;
        vec3 body_col = vec3(0.0);
        
        vec2 body_pos = (cloud_density == 0.0) ? vec2(0.0, 0.0) : vec2(0.35, 0.35);
        
        if (storm_intensity == 0.0) {
            if (u_is_night == 1) {
                float d1 = sdCircle(uv - body_pos, 0.35);
                float d2 = sdCircle(uv - (body_pos + vec2(0.12, 0.08)), 0.3);
                moon_dist = max(d1, -d2);
                body_col = vec3(0.9, 0.95, 1.0);
            } else {
                sun_dist = sdCircle(uv - body_pos, 0.35);
                body_col = vec3(1.0, 0.8, 0.2);
                float pulse = 1.0 + 0.04 * sin(u_time * 2.0);
                corona_dist = sdCircle(uv - body_pos, 0.35 * pulse);
            }
        }
        
        // Storm Lightning Flash calculation
        float flash = 0.0;
        float bolt_alpha = 0.0;
        float bolt_glow = 0.0;
        
        if (storm_intensity > 0.0) {
            float cycle = u_time * 0.25;
            float cycle_id = floor(cycle);
            float cycle_f = fract(cycle);
            float r = hash(vec2(cycle_id, 45.67));
            if (r > 0.5) {
                float flash1 = max(0.0, 1.0 - (cycle_f - 0.1) * 8.0) * smoothstep(0.1, 0.11, cycle_f);
                float flash2 = max(0.0, 1.0 - (cycle_f - 0.25) * 6.0) * smoothstep(0.25, 0.26, cycle_f);
                flash = max(flash1, flash2 * 0.8) * storm_intensity;
                
                if (cycle_f > 0.1 && cycle_f < 0.25) {
                    float bolt_x = (hash(vec2(cycle_id, 12.3)) - 0.5) * 0.5;
                    vec2 bolt_pos = uv - vec2(bolt_x, 0.1);
                    float l_dist = sdLightning(bolt_pos * 1.6);
                    bolt_alpha = 1.0 - smoothstep(0.0, blur, l_dist);
                    bolt_glow = (1.0 - smoothstep(0.0, 0.25, l_dist)) * 0.5;
                }
            }
        }
        
        // Render Sky Background & Flash Glow
        if (flash > 0.0) {
            col = mix(col, vec3(0.5, 0.6, 0.85), flash * 0.25);
            final_alpha = max(final_alpha, flash * 0.25);
        }
        
        // Render Sun/Moon/Stars
        if (u_is_night == 0 && storm_intensity == 0.0) {
            float sun_alpha = 1.0 - smoothstep(0.0, blur, sun_dist);
            float corona_alpha = (1.0 - smoothstep(0.0, 0.35, corona_dist)) * 0.45;
            
            vec2 to_sun = uv - body_pos;
            float angle = atan(to_sun.y, to_sun.x);
            float dist_to_sun = length(to_sun);
            float ray = sin(angle * 8.0 + u_time * 0.5) * 0.5 + 0.5;
            float ray_fade = smoothstep(1.2, 0.35, dist_to_sun);
            float ray_alpha = ray * ray_fade * 0.25 * (1.0 - cloud_density);
            
            col = mix(col, vec3(1.0, 0.65, 0.0), corona_alpha);
            final_alpha = max(final_alpha, corona_alpha);
            
            col = mix(col, vec3(1.0, 0.9, 0.5), ray_alpha);
            final_alpha = max(final_alpha, ray_alpha);
            
            col = mix(col, vec3(1.0, 0.95, 0.6), sun_alpha);
            final_alpha = max(final_alpha, sun_alpha);
        } else if (u_is_night == 1 && storm_intensity == 0.0) {
            float moon_alpha = 1.0 - smoothstep(0.0, blur, moon_dist);
            col = mix(col, body_col, moon_alpha);
            final_alpha = max(final_alpha, moon_alpha);
            
            float glow_alpha = (1.0 - smoothstep(0.0, 0.4, sdCircle(uv - body_pos, 0.35))) * 0.35;
            col = mix(col, vec3(0.4, 0.65, 1.0), glow_alpha * (1.0 - moon_alpha));
            final_alpha = max(final_alpha, glow_alpha);
            
            vec2 star_uv = uv * 6.0;
            vec2 star_id = floor(star_uv);
            vec2 star_f = fract(star_uv) - 0.5;
            float r = hash(star_id);
            if (r > 0.9) {
                float star_twinkle = 0.3 + 0.7 * sin(u_time * (2.5 + r * 2.0) + r * 6.28);
                float star_d = length(star_f - (r * 0.6 - 0.3));
                float star_radius = 0.01 + r * 0.015;
                float star_a = (1.0 - smoothstep(star_radius * 0.5, star_radius * 1.5, star_d)) * star_twinkle * (1.0 - cloud_density);
                
                col = mix(col, vec3(1.0), star_a);
                final_alpha = max(final_alpha, star_a);
            }
        }
        
        // --- Layer 2: Background Cloud ---
        float bcloud_alpha = 0.0;
        if (cloud_density > 0.0) {
            float drift = u_time * (0.15 + u_wind_speed * 0.005);
            vec2 c_uv = uv - vec2(0.25 * sin(drift) - 0.15, 0.1);
            float cloud_dist = sdCloud(c_uv * 1.2);
            
            if (cloud_dist < 0.25) {
                float shadow = 1.0 - smoothstep(0.0, 0.2, cloud_dist - 0.1);
                bcloud_alpha = 1.0 - smoothstep(0.0, blur, cloud_dist);
                
                vec3 c_base = (u_is_night == 1) ? vec3(0.2, 0.24, 0.32) : vec3(0.82, 0.86, 0.9);
                if (rain_intensity > 0.0 || storm_intensity > 0.0) {
                    c_base = (u_is_night == 1) ? vec3(0.1, 0.12, 0.16) : vec3(0.4, 0.43, 0.48);
                }
                
                float cloud_grad = clamp((uv.y - c_uv.y + 0.2) / 0.4, 0.0, 1.0);
                vec3 c_col = mix(c_base * 0.65, c_base * 1.15, cloud_grad);
                
                c_col += vec3(0.4, 0.45, 0.6) * flash;
                
                col = mix(col, vec3(0.0), shadow * 0.35 * (1.0 - bcloud_alpha));
                final_alpha = max(final_alpha, shadow * 0.35);
                
                float target_opacity = clamp(cloud_density * 1.5 - 0.5, 0.0, 1.0);
                col = mix(col, c_col, bcloud_alpha * target_opacity);
                final_alpha = max(final_alpha, bcloud_alpha * target_opacity);
            }
        }
        
        // --- Layer 3: Lightning Bolt ---
        if (bolt_alpha > 0.0 || bolt_glow > 0.0) {
            col = mix(col, vec3(1.0, 0.95, 0.8), bolt_glow * flash);
            final_alpha = max(final_alpha, bolt_glow * flash);
            
            col = mix(col, vec3(1.0), bolt_alpha);
            final_alpha = max(final_alpha, bolt_alpha);
        }
        
        // --- Layer 4: Rain / Snow / Hail / Ripples ---
        float wind = u_wind_speed * 0.008 + 0.05 * sin(u_time * 1.5);
        
        // Rain particles
        if (rain_intensity > 0.0) {
            float rain_acc = 0.0;
            vec3 rain_particle_col = vec3(0.0);
            
            for (int i = 0; i < 3; i++) {
                float fi = float(i);
                float scale = 4.0 + fi * 3.0;
                float speed = 1.6 + fi * 0.7;
                float opacity = 0.3 + fi * 0.15;
                
                vec2 p_uv = uv;
                p_uv.x -= wind * p_uv.y;
                p_uv.y += u_time * speed;
                
                vec2 g_uv = p_uv * scale;
                vec2 id = floor(g_uv);
                vec2 f = fract(g_uv) - 0.5;
                
                float r = hash(id);
                float density = rain_intensity * (0.85 - fi * 0.15);
                if (r < density) {
                    vec2 offset = vec2(r * 0.6 - 0.3, fract(r * 5.0) * 0.6 - 0.3);
                    float streak_y = f.y - offset.y;
                    float streak_x = f.x - offset.x;
                    
                    vec2 cap_a = vec2(0.0, 0.16);
                    vec2 cap_b = vec2(0.0, -0.16);
                    float dist = sdCapsule(vec2(streak_x, streak_y), cap_a, cap_b, 0.015);
                    
                    float fade = smoothstep(-0.25, 0.15, streak_y);
                    float a = (1.0 - smoothstep(0.0, blur, dist)) * fade * opacity;
                    vec3 col_layer = (u_is_night == 1) ? vec3(0.3, 0.45, 0.6) : vec3(0.68, 0.78, 0.92);
                    
                    rain_acc = max(rain_acc, a);
                    rain_particle_col = mix(rain_particle_col, col_layer, a);
                }
            }
            
            float bottom_mask = smoothstep(-0.85, -0.75, uv.y);
            rain_acc *= bottom_mask;
            
            col = mix(col, rain_particle_col, rain_acc);
            final_alpha = max(final_alpha, rain_acc);
        }
        
        // Snow particles
        if (snow_intensity > 0.0) {
            float snow_acc = 0.0;
            vec3 snow_particle_col = vec3(0.0);
            
            for (int i = 0; i < 3; i++) {
                float fi = float(i);
                float scale = 4.0 + fi * 3.0;
                float speed = 0.35 + fi * 0.15;
                float opacity = 0.45 + fi * 0.15;
                
                vec2 p_uv = uv;
                p_uv.y += u_time * speed;
                
                float flutter = sin(u_time * (1.5 + fi * 0.5) + p_uv.y * (3.0 + fi)) * (0.06 + fi * 0.03);
                p_uv.x += flutter + wind * 0.35 * p_uv.y;
                
                vec2 g_uv = p_uv * scale;
                vec2 id = floor(g_uv);
                vec2 f = fract(g_uv) - 0.5;
                
                float r = hash(id);
                float density = snow_intensity * (0.8 - fi * 0.1);
                if (r < density) {
                    vec2 offset = vec2(r * 0.6 - 0.3, fract(r * 7.0) * 0.6 - 0.3);
                    float d = length(f - offset);
                    float radius = 0.035 + r * 0.03;
                    float a = smoothstep(radius, radius - 0.035, d) * opacity;
                    
                    snow_acc = max(snow_acc, a);
                    snow_particle_col = mix(snow_particle_col, vec3(0.95, 0.98, 1.0), a);
                }
            }
            
            float bottom_mask = smoothstep(-0.85, -0.75, uv.y);
            snow_acc *= bottom_mask;
            
            col = mix(col, snow_particle_col, snow_acc);
            final_alpha = max(final_alpha, snow_acc);
        }
        
        // Hail particles
        if (hail_intensity > 0.0) {
            float hail_acc = 0.0;
            vec3 hail_particle_col = vec3(0.0);
            
            for (int i = 0; i < 2; i++) {
                float fi = float(i);
                float scale = 6.0 + fi * 4.0;
                float speed = 2.5 + fi * 1.0;
                float opacity = 0.5 + fi * 0.2;
                
                vec2 p_uv = uv;
                p_uv.x -= wind * p_uv.y;
                p_uv.y += u_time * speed;
                
                vec2 g_uv = p_uv * scale;
                vec2 id = floor(g_uv);
                vec2 f = fract(g_uv) - 0.5;
                
                float r = hash(id);
                if (r < hail_intensity * 0.6) {
                    vec2 offset = vec2(r * 0.6 - 0.3, fract(r * 11.0) * 0.6 - 0.3);
                    float d = length(f - offset);
                    float a = (1.0 - smoothstep(0.012, 0.035, d)) * opacity;
                    
                    hail_acc = max(hail_acc, a);
                    hail_particle_col = mix(hail_particle_col, vec3(0.88, 0.94, 1.0), a);
                }
            }
            
            float bottom_mask = smoothstep(-0.85, -0.75, uv.y);
            hail_acc *= bottom_mask;
            
            col = mix(col, hail_particle_col, hail_acc);
            final_alpha = max(final_alpha, hail_acc);
        }
        
        // Splash ripples
        if ((rain_intensity > 0.0 || hail_intensity > 0.0) && uv.y < -0.4) {
            float ripple_acc = 0.0;
            vec3 ripple_col = (u_is_night == 1) ? vec3(0.25, 0.4, 0.55) : vec3(0.7, 0.8, 0.92);
            
            float scale_x = 8.0;
            float id_x = floor(uv.x * scale_x);
            float f_x = fract(uv.x * scale_x) - 0.5;
            
            float r = hash(vec2(id_x, 137.45));
            float threshold = max(rain_intensity, hail_intensity) * 0.75;
            if (r < threshold) {
                float cycle_speed = 2.5 + r * 2.0;
                float t = fract(u_time * cycle_speed + r);
                
                float splash_x = r * 0.6 - 0.3;
                float splash_y = -0.72 - fract(r * 4.3) * 0.1;
                
                vec2 splash_center = vec2(splash_x, splash_y);
                vec2 diff = uv - splash_center;
                diff.y *= 2.5;
                
                float dist = length(diff);
                float radius = t * 0.14;
                float ring_dist = abs(dist - radius);
                
                float ripple = (1.0 - smoothstep(0.005, 0.02, ring_dist)) * (1.0 - t) * 0.45;
                
                float droplet = 0.0;
                if (t < 0.4) {
                    float ht = sin(t * 3.14159 / 0.4) * 0.08;
                    float drop_d = length(uv - (splash_center + vec2(0.0, ht)));
                    droplet = (1.0 - smoothstep(0.0, 0.015, drop_d)) * (1.0 - t * 2.5) * 0.6;
                }
                
                ripple_acc = max(ripple, droplet);
            }
            
            col = mix(col, ripple_col, ripple_acc);
            final_alpha = max(final_alpha, ripple_acc);
        }
        
        // --- Layer 5: Foreground Cloud ---
        float fcloud_alpha = 0.0;
        if (cloud_density > 0.25) {
            float drift = u_time * (0.25 + u_wind_speed * 0.008);
            vec2 c_uv = uv - vec2(-0.15 * sin(drift) + 0.1, -0.15);
            float cloud_dist = sdCloud(c_uv * 1.0);
            
            if (cloud_dist < 0.25) {
                float shadow = 1.0 - smoothstep(0.0, 0.22, cloud_dist - 0.1);
                fcloud_alpha = 1.0 - smoothstep(0.0, blur, cloud_dist);
                
                vec3 c_base = (u_is_night == 1) ? vec3(0.3, 0.35, 0.45) : vec3(0.98, 0.98, 1.0);
                if (rain_intensity > 0.0 || storm_intensity > 0.0) {
                    c_base = (u_is_night == 1) ? vec3(0.16, 0.2, 0.26) : vec3(0.55, 0.58, 0.62);
                }
                
                float cloud_grad = clamp((uv.y - c_uv.y + 0.18) / 0.36, 0.0, 1.0);
                vec3 c_col = mix(c_base * 0.7, c_base * 1.2, cloud_grad);
                
                c_col += vec3(0.5, 0.55, 0.7) * flash;
                
                col = mix(col, vec3(0.0), shadow * 0.45 * (1.0 - fcloud_alpha));
                final_alpha = max(final_alpha, shadow * 0.45);
                
                float target_opacity = clamp(cloud_density * 1.5, 0.0, 1.0);
                col = mix(col, c_col, fcloud_alpha * target_opacity);
                final_alpha = max(final_alpha, fcloud_alpha * target_opacity);
            }
        }
        
        // --- Layer 6: Drifting Fog ---
        if (fog_density > 0.0) {
            float mist1 = sin(uv.x * 2.0 + u_time * (0.3 + u_wind_speed * 0.02)) * cos(uv.y * 2.5 + u_time * 0.15);
            float mist2 = sin(uv.x * 4.5 - u_time * (0.5 + u_wind_speed * 0.03)) * cos(uv.y * 1.2 - u_time * 0.25);
            float mist = (mist1 * 0.5 + 0.5) * (mist2 * 0.5 + 0.5);
            
            float height_mask = smoothstep(-0.9, -0.4, uv.y) * (1.0 - smoothstep(0.3, 0.7, uv.y));
            float fog_alpha = mist * fog_density * 0.35 * height_mask;
            
            vec3 fog_col = (u_is_night == 1) ? vec3(0.15, 0.18, 0.25) : vec3(0.85, 0.88, 0.92);
            col = mix(col, fog_col, fog_alpha);
            final_alpha = max(final_alpha, fog_alpha);
        }
        
        gl_FragColor = vec4(col, clamp(final_alpha, 0.0, 1.0));
    }
)";

Renderer::Renderer() : program_(0), position_loc_(0), tex_coord_loc_(0), sampler_loc_(0), matrix_loc_(0), color_loc_(0), weather_program_(0), weather_pos_loc_(0), weather_matrix_loc_(0), weather_time_loc_(0), weather_code_loc_(0), weather_is_night_loc_(0), weather_wind_speed_loc_(0), weather_coord_loc_(0), vbo_(0), white_texture_(0), width_(0), height_(0) {
    for (int i = 0; i < 16; i++) matrix_[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

Renderer::~Renderer() {
    if (program_) glDeleteProgram(program_);
    if (weather_program_) glDeleteProgram(weather_program_);
    if (white_texture_) {
        GLuint tid = white_texture_;
        glDeleteTextures(1, &tid);
    }
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
    weather_wind_speed_loc_ = glGetUniformLocation(weather_program_, "u_wind_speed");

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

void Renderer::draw_animated_weather(int weather_code, float x, float y, float w, float h, float time_sec, bool is_night, float wind_speed) {
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
    glUniform1f(weather_wind_speed_loc_, wind_speed);

    glVertexAttribPointer(weather_pos_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(weather_pos_loc_);

    glVertexAttribPointer(weather_coord_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(weather_coord_loc_);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

} // namespace nuc_display::core
