#pragma once

#include "compression/coder/coder_interfaces.hpp"

#include <cstdint>
#include <vector>

namespace winzox::extraction::decoder {

struct DecodePipelineRequest {
    std::vector<uint8_t> input;
    compression::coder::CoderKind coder = compression::coder::CoderKind::Raw;
    size_t expectedSize = 0;
    size_t dictionarySize = 0;
};

std::vector<uint8_t> RunDecodePipeline(const DecodePipelineRequest& request);

} // namespace winzox::extraction::decoder
