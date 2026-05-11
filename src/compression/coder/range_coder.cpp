#include "compression/coder/range_coder.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

// Byte-oriented carry-less range coder (Subbotin variant) driven by an
// order-0 adaptive frequency model.
//
// Frame layout (encoded output):
//   bytes 0..3 : little-endian uint32 == decoded length
//   bytes 4..N : range-coded payload
//
// The decoded-length prefix lets the decoder bound its work and lets the
// frame round-trip exactly even when the input is empty.

namespace winzox::compression::coder {

namespace {

constexpr uint32_t kTopValue = 1u << 24;       // 0x01000000
constexpr uint32_t kBottomValue = 1u << 16;    // 0x00010000
constexpr uint32_t kBottomMask = kBottomValue - 1u;
constexpr uint32_t kInitialRange = 0xFFFFFFFFu;

// Frequency cap; on overflow all counts are halved (rounded up to keep
// every symbol reachable).
constexpr uint32_t kFrequencyCap = 1u << 14;

struct Model {
    std::array<uint32_t, 257> cumulative {};

    Model() {
        for (size_t i = 0; i <= 256; ++i) {
            cumulative[i] = static_cast<uint32_t>(i);
        }
    }

    [[nodiscard]] uint32_t Total() const {
        return cumulative[256];
    }
    [[nodiscard]] uint32_t LowOf(uint8_t symbol) const {
        return cumulative[symbol];
    }
    [[nodiscard]] uint32_t HighOf(uint8_t symbol) const {
        return cumulative[symbol + 1];
    }

    void Update(uint8_t symbol) {
        for (size_t i = symbol + 1; i < cumulative.size(); ++i) {
            cumulative[i] += 1;
        }
        if (cumulative[256] >= kFrequencyCap) {
            uint32_t newTotal = 0;
            std::array<uint32_t, 257> next {};
            for (size_t i = 0; i < 256; ++i) {
                const uint32_t count = cumulative[i + 1] - cumulative[i];
                const uint32_t halved = (count >> 1) | 1u;
                next[i] = newTotal;
                newTotal += halved;
            }
            next[256] = newTotal;
            cumulative = next;
        }
    }

    // Linear scan over the 256-symbol alphabet to locate the symbol that
    // owns the given scaled value. Used by the decoder.
    [[nodiscard]] uint8_t SymbolFor(uint32_t scaled) const {
        for (size_t s = 0; s < 256; ++s) {
            if (scaled < cumulative[s + 1]) {
                return static_cast<uint8_t>(s);
            }
        }
        return 255;
    }
};

class Encoder {
public:
    explicit Encoder(std::vector<uint8_t>& output) : output_(output) {}

    void EncodeSymbol(const Model& model, uint8_t symbol) {
        range_ /= model.Total();
        low_ += model.LowOf(symbol) * range_;
        range_ *= (model.HighOf(symbol) - model.LowOf(symbol));
        Renormalize();
    }

    void Finish() {
        for (int i = 0; i < 5; ++i) {
            EmitByte(static_cast<uint8_t>(low_ >> 24));
            low_ <<= 8;
        }
    }

private:
    void EmitByte(uint8_t byte) {
        output_.push_back(byte);
    }

    void Renormalize() {
        while (true) {
            const bool topConverged = (low_ ^ (low_ + range_)) < kTopValue;
            if (topConverged) {
                EmitByte(static_cast<uint8_t>(low_ >> 24));
                range_ <<= 8;
                low_ <<= 8;
                continue;
            }
            if (range_ < kBottomValue) {
                // Underflow recovery: clamp range to the largest value
                // that keeps the high bits unchanged.
                range_ = (-static_cast<int32_t>(low_)) & kBottomMask;
                EmitByte(static_cast<uint8_t>(low_ >> 24));
                range_ <<= 8;
                low_ <<= 8;
                continue;
            }
            break;
        }
    }

    std::vector<uint8_t>& output_;
    uint32_t low_ = 0;
    uint32_t range_ = kInitialRange;
};

class Decoder {
public:
    Decoder(const std::vector<uint8_t>& input, size_t startOffset)
        : input_(input), pos_(startOffset) {
        for (int i = 0; i < 4; ++i) {
            code_ = (code_ << 8) | NextByte();
        }
    }

    uint8_t DecodeSymbol(const Model& model) {
        const uint32_t total = model.Total();
        range_ /= total;
        if (range_ == 0) {
            throw std::runtime_error("Range coder: zero range during decode");
        }
        const uint32_t scaled = (code_ - low_) / range_;
        if (scaled >= total) {
            throw std::runtime_error("Range coder: scaled value out of range");
        }
        const uint8_t symbol = model.SymbolFor(scaled);
        low_ += model.LowOf(symbol) * range_;
        range_ *= (model.HighOf(symbol) - model.LowOf(symbol));
        Renormalize();
        return symbol;
    }

private:
    uint8_t NextByte() {
        if (pos_ >= input_.size()) {
            return 0;
        }
        return input_[pos_++];
    }

    void Renormalize() {
        while (true) {
            const bool topConverged = (low_ ^ (low_ + range_)) < kTopValue;
            if (topConverged) {
                code_ = (code_ << 8) | NextByte();
                range_ <<= 8;
                low_ <<= 8;
                continue;
            }
            if (range_ < kBottomValue) {
                range_ = (-static_cast<int32_t>(low_)) & kBottomMask;
                code_ = (code_ << 8) | NextByte();
                range_ <<= 8;
                low_ <<= 8;
                continue;
            }
            break;
        }
    }

    const std::vector<uint8_t>& input_;
    size_t pos_;
    uint32_t low_ = 0;
    uint32_t range_ = kInitialRange;
    uint32_t code_ = 0;
};

void AppendU32LE(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFFu));
}

uint32_t ReadU32LE(const std::vector<uint8_t>& in, size_t offset) {
    return static_cast<uint32_t>(in[offset]) |
           (static_cast<uint32_t>(in[offset + 1]) << 8) |
           (static_cast<uint32_t>(in[offset + 2]) << 16) |
           (static_cast<uint32_t>(in[offset + 3]) << 24);
}

} // namespace

CoderKind RangeEncoder::Kind() const {
    return CoderKind::Range;
}

std::vector<uint8_t> RangeEncoder::Encode(const std::vector<uint8_t>& input,
                                          const EncodeOptions& /*options*/) const {
    if (input.size() > static_cast<size_t>(0xFFFFFFFFu)) {
        throw std::runtime_error("Range coder: input exceeds 2^32-1 bytes");
    }

    std::vector<uint8_t> output;
    output.reserve(4 + input.size() / 2 + 16);
    AppendU32LE(output, static_cast<uint32_t>(input.size()));

    if (input.empty()) {
        return output;
    }

    Encoder encoder(output);
    Model model;
    for (uint8_t symbol : input) {
        encoder.EncodeSymbol(model, symbol);
        model.Update(symbol);
    }
    encoder.Finish();
    return output;
}

CoderKind RangeDecoder::Kind() const {
    return CoderKind::Range;
}

std::vector<uint8_t> RangeDecoder::Decode(const std::vector<uint8_t>& input,
                                          const DecodeOptions& options) const {
    if (input.size() < 4) {
        throw std::runtime_error("Range coder: encoded frame is truncated");
    }
    const uint32_t length = ReadU32LE(input, 0);
    if (options.expectedSize != 0 && options.expectedSize != length) {
        throw std::runtime_error("Range coder: declared length does not match expected size");
    }
    if (length == 0) {
        return {};
    }

    Decoder decoder(input, 4);
    Model model;
    std::vector<uint8_t> output;
    output.reserve(length);
    for (uint32_t i = 0; i < length; ++i) {
        const uint8_t symbol = decoder.DecodeSymbol(model);
        output.push_back(symbol);
        model.Update(symbol);
    }
    return output;
}

} // namespace winzox::compression::coder
