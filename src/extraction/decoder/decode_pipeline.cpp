#include "extraction/decoder/decode_pipeline.hpp"

#include "compression/coder/huffman.hpp"
#include "compression/coder/range_coder.hpp"
#include "compression/dictionary/dictionary_manager.hpp"

#include <stdexcept>

namespace winzox::extraction::decoder {

std::vector<uint8_t> RunDecodePipeline(const DecodePipelineRequest& request) {
    using winzox::compression::coder::DecodeOptions;

    const DecodeOptions options { request.expectedSize, request.dictionarySize };
    std::vector<uint8_t> decoded;

    switch (request.coder) {
    case winzox::compression::coder::CoderKind::Raw:
        decoded = request.input;
        break;

    case winzox::compression::coder::CoderKind::Huffman: {
        const winzox::compression::coder::HuffmanDecoder decoder;
        decoded = decoder.Decode(request.input, options);
        break;
    }

    case winzox::compression::coder::CoderKind::Range: {
        const winzox::compression::coder::RangeDecoder decoder;
        decoded = decoder.Decode(request.input, options);
        break;
    }
    }

    if (request.dictionarySize > 0) {
        winzox::compression::dictionary::DictionaryWindow dictionary(request.dictionarySize);
        dictionary.Append(decoded);
    }

    return decoded;
}

} // namespace winzox::extraction::decoder
