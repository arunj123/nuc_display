#pragma once

#include "modules/media_module.hpp"
#include <vector>
#include <string>
#include <expected>
#include <cstdint>

namespace nuc_display::modules {

class ImageLoader : public MediaModule {
public:
    ImageLoader() = default;
    ~ImageLoader() override = default;

    std::expected<void, MediaError> load(const std::string& filepath) override;
    std::expected<void, MediaError> process(double time_sec) override;

    // Accessors for generic image data
    const std::vector<uint8_t>& get_rgba_data() const { return rgba_data_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    uint32_t channels() const { return channels_; }

private:
    std::expected<void, MediaError> load_jpeg(const std::string& filepath);
    std::expected<void, MediaError> load_png(const std::string& filepath);

    std::vector<uint8_t> rgba_data_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t channels_ = 0;
};

} // namespace nuc_display::modules
