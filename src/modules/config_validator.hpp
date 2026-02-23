#pragma once

#include "modules/config_module.hpp"
#include <string>
#include <vector>

namespace nuc_display::modules {

class ConfigValidator {
public:
    // Returns a list of error messages. Empty = valid.
    static std::vector<std::string> validate(const AppConfig& config);
};

} // namespace nuc_display::modules
