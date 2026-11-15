// =============================================================================
// fq-compressor - Shared fqzcomp Quality Codec
// =============================================================================

#include "fqc/format/quality_codec.h"

#include <cstdlib>
#include <limits>
#include <utility>

#include "fqzcomp_qual.h"

namespace fqc::format {

namespace {

using Bytes = std::vector<std::uint8_t>;

void appendVarint(Bytes& output, std::uint64_t value) {
    while (value >= 0x80U) {
        output.push_back(static_cast<std::uint8_t>(value) | 0x80U);
        value >>= 7U;
    }
    output.push_back(static_cast<std::uint8_t>(value));
}

[[nodiscard]] auto checkedAdd(std::size_t& destination, std::size_t value) noexcept -> bool {
    if (value > std::numeric_limits<std::size_t>::max() - destination) {
        return false;
    }
    destination += value;
    return true;
}

}  // namespace

auto compressFqzQuality(std::span<const ReadRecord> records) -> Result<FqzQualityResult> {
    if (records.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return makeError<FqzQualityResult>(ErrorCode::kUsageError,
                                           "block has too many records for fqzcomp");
    }

    std::vector<std::uint32_t> lengths;
    std::vector<std::uint32_t> flags;
    Bytes values;
    lengths.reserve(records.size());
    flags.resize(records.size(), 0);
    for (const auto& record : records) {
        if (record.quality.size() > std::numeric_limits<std::uint32_t>::max()) {
            return makeError<FqzQualityResult>(ErrorCode::kUsageError,
                                               "quality record is too long for fqzcomp");
        }
        lengths.push_back(static_cast<std::uint32_t>(record.quality.size()));
        for (const char value : record.quality) {
            if (value < '!' || value > '~') {
                return makeError<FqzQualityResult>(ErrorCode::kUsageError,
                                                   "invalid quality byte in fqzcomp block");
            }
            values.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(value) - 33U));
        }
    }

    FqzQualityResult best;
    bool haveBest = false;
    fqz_slice slice{};
    slice.num_records = static_cast<int>(records.size());
    slice.len = lengths.data();
    slice.flags = flags.data();

    for (const int strategy : {0, 1}) {
        std::vector<char> input(values.begin(), values.end());
        std::size_t compressedSize = 0;
        auto* compressed =
            fqz_compress(4, &slice, input.data(), input.size(), &compressedSize, strategy, nullptr);
        if (compressed == nullptr) {
            continue;
        }
        Bytes candidate(compressed, compressed + compressedSize);
        std::free(compressed);
        if (!haveBest || candidate.size() < best.bytes.size()) {
            best.bytes = std::move(candidate);
            best.strategy = strategy;
            haveBest = true;
        }
    }

    if (!haveBest) {
        return makeError<FqzQualityResult>(ErrorCode::kInternalError,
                                           "fqzcomp quality compression failed");
    }
    return best;
}

auto decompressFqzQuality(std::span<const std::uint8_t> payload,
                          std::uint32_t recordCount,
                          std::size_t maxRawBytes) -> Result<std::vector<std::uint8_t>> {
    if (recordCount > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        return makeError<std::vector<std::uint8_t>>(ErrorCode::kFormatError,
                                                    "fqzcomp record count is too large");
    }
    std::vector<int> lengths(recordCount);
    std::size_t restoredSize = 0;
    auto* restored =
        fqz_decompress(const_cast<char*>(reinterpret_cast<const char*>(payload.data())),
                       payload.size(),
                       &restoredSize,
                       lengths.data(),
                       static_cast<int>(lengths.size()));
    if (restored == nullptr) {
        return makeError<std::vector<std::uint8_t>>(ErrorCode::kFormatError,
                                                    "fqzcomp payload failed to decode");
    }

    Bytes raw;
    appendVarint(raw, recordCount);
    std::size_t total = 0;
    for (const int length : lengths) {
        if (length < 0 || static_cast<std::size_t>(length) > maxRawBytes ||
            !checkedAdd(total, static_cast<std::size_t>(length))) {
            std::free(restored);
            return makeError<std::vector<std::uint8_t>>(ErrorCode::kFormatError,
                                                        "fqzcomp length is invalid");
        }
    }
    if (total != restoredSize) {
        std::free(restored);
        return makeError<std::vector<std::uint8_t>>(ErrorCode::kFormatError,
                                                    "fqzcomp output size disagrees with lengths");
    }

    // Match v2's raw quality stream exactly: for each record, varint(length)
    // followed immediately by the ASCII quality bytes.
    std::size_t offset = 0;
    for (const int length : lengths) {
        const auto size = static_cast<std::size_t>(length);
        appendVarint(raw, size);
        for (std::size_t i = 0; i < size; ++i) {
            const auto value = static_cast<unsigned char>(restored[offset + i]);
            if (value > 93U) {
                std::free(restored);
                return makeError<std::vector<std::uint8_t>>(ErrorCode::kFormatError,
                                                            "quality value is out of range");
            }
            raw.push_back(static_cast<std::uint8_t>(value + 33U));
        }
        offset += size;
    }
    std::free(restored);
    return raw;
}

}  // namespace fqc::format
