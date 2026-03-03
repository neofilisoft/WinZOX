#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace zipbox::utils {

struct ProgressInfo {
    uint64_t completedUnits = 0;
    uint64_t totalUnits = 0;
    std::string currentItem;
    std::string statusText;
};

using ProgressCallback = std::function<bool(const ProgressInfo&)>;

} // namespace zipbox::utils
