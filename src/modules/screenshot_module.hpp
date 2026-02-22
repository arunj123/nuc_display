#pragma once

#include "modules/media_module.hpp"
#include <string>
#include <vector>
#include <expected>
#include <cstdint>

namespace nuc_display::modules {

class ScreenshotModule {
public:
    ScreenshotModule() = default;
    ~ScreenshotModule() = default;

    // Captures the current framebuffer (GLES2)
    std::expected<void, MediaError> capture(int width, int height);
    
    // Saves the captured buffer to a file
    std::expected<void, MediaError> save(const std::string& filepath);

private:
    std::vector<uint8_t> pixel_data_;
    int width_ = 0;
    int height_ = 0;
};

} // namespace nuc_display::modules
