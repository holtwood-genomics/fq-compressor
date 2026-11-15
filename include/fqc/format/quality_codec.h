// =============================================================================
// fq-compressor - Shared fqzcomp Quality Codec
// =============================================================================

#pragma once

#include "fqc/common/error.h"
#include "fqc/common/types.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace fqc::format {

/// One fqzcomp-compressed quality payload plus the strategy that produced it.
struct FqzQualityResult {
    std::vector<std::uint8_t> bytes;
    int strategy = 0;
};

/// Compress the quality values of @p records with htscodecs fqzcomp. The input
/// is the v2 logical quality stream (per-record length + ASCII quality), but
/// only the quality symbols are fed to fqzcomp; the lengths are carried in the
/// fqzcomp slice and reconstructed by the decoder.
[[nodiscard]] auto compressFqzQuality(std::span<const ReadRecord> records)
    -> Result<FqzQualityResult>;

/// Decode an fqzcomp quality payload back into the v2 logical quality stream:
/// `varint(recordCount)` followed by `varint(length) + ASCII quality` per
/// record. @p maxRawBytes bounds the reconstructed stream.
[[nodiscard]] auto decompressFqzQuality(std::span<const std::uint8_t> payload,
                                        std::uint32_t recordCount,
                                        std::size_t maxRawBytes)
    -> Result<std::vector<std::uint8_t>>;

}  // namespace fqc::format
