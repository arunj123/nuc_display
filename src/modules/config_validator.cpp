#include "modules/config_validator.hpp"
#include <set>
#include <sstream>

namespace nuc_display::modules {

std::vector<std::string> ConfigValidator::validate(const AppConfig& config) {
    std::vector<std::string> errors;

    // 1. Location validation
    if (config.location.lat < -90.0f || config.location.lat > 90.0f) {
        errors.push_back("location.lat out of range [-90, 90]: " + std::to_string(config.location.lat));
    }
    if (config.location.lon < -180.0f || config.location.lon > 180.0f) {
        errors.push_back("location.lon out of range [-180, 180]: " + std::to_string(config.location.lon));
    }

    // 2. Stock validation
    if (config.stocks.empty()) {
        errors.push_back("No stock symbols configured.");
    }

    // 3. Key uniqueness check
    std::set<uint16_t> used_keys;
    auto check_key = [&](uint16_t code, const std::string& context) {
        if (code == 0) return; // 0 means unset/auto
        if (!used_keys.insert(code).second) {
            errors.push_back("Duplicate key binding: '" + key_code_to_name(code) + "' (code " + std::to_string(code) + ") in " + context);
        }
    };

    // Global keys
    if (config.global_keys.hide_videos) {
        check_key(*config.global_keys.hide_videos, "global_keys.hide_videos");
    }

    // Stock keys
    if (config.stock_keys.next_stock) check_key(*config.stock_keys.next_stock, "stock_keys.next_stock");
    if (config.stock_keys.prev_stock) check_key(*config.stock_keys.prev_stock, "stock_keys.prev_stock");
    if (config.stock_keys.next_chart) check_key(*config.stock_keys.next_chart, "stock_keys.next_chart");
    if (config.stock_keys.prev_chart) check_key(*config.stock_keys.prev_chart, "stock_keys.prev_chart");

    // 4. Per-video validation
    for (size_t i = 0; i < config.videos.size(); ++i) {
        const auto& v = config.videos[i];
        std::string ctx = "videos[" + std::to_string(i) + "]";

        // Enabled videos must have playlists
        if (v.enabled && v.playlists.empty()) {
            errors.push_back(ctx + ": enabled but has no playlists.");
        }

        // Coordinate range checks
        auto check_range = [&](float val, const std::string& name) {
            if (val < 0.0f || val > 1.0f) {
                errors.push_back(ctx + "." + name + " out of range [0.0, 1.0]: " + std::to_string(val));
            }
        };
        check_range(v.x, "x");
        check_range(v.y, "y");
        check_range(v.w, "w");
        check_range(v.h, "h");
        check_range(v.src_x, "src_x");
        check_range(v.src_y, "src_y");
        check_range(v.src_w, "src_w");
        check_range(v.src_h, "src_h");

        // Start trigger key uniqueness
        if (v.start_trigger_key > 0) {
            check_key(v.start_trigger_key, ctx + ".start_trigger");
        }

        // Navigation key uniqueness
        if (v.keys.next) check_key(*v.keys.next, ctx + ".keys.next");
        if (v.keys.prev) check_key(*v.keys.prev, ctx + ".keys.prev");
        if (v.keys.skip_forward) check_key(*v.keys.skip_forward, ctx + ".keys.skip_forward");
        if (v.keys.skip_backward) check_key(*v.keys.skip_backward, ctx + ".keys.skip_backward");
    }

    return errors;
}

} // namespace nuc_display::modules
