#pragma once

#include <string>
#include <expected>

namespace nuc_display::modules {

enum class MediaError {
    FileNotFound,
    UnsupportedFormat,
    DecodeFailed,
    HardwareError,
    InternalError
};

class MediaModule {
public:
    virtual ~MediaModule() = default;

    // Load media from a file path
    virtual std::expected<void, MediaError> load(const std::string& filepath) = 0;
    
    // Process/Render a frame slice (time-based for video/audio, static for images)
    virtual std::expected<void, MediaError> process(double time_sec) = 0;
};

} // namespace nuc_display::modules
