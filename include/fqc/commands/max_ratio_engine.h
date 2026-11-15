// =============================================================================
// fq-compressor - Experimental Max-Ratio Engine
// =============================================================================

#pragma once

#include "fqc/commands/archive_engine.h"
#include "fqc/common/error.h"
#include "fqc/format/max_ratio.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace fqc::commands {

/// Inputs for the experimental ratio-first compressor. Block sizing and the
/// memory limit are enforced before any block is written.
struct MaxCompressionRequest {
    std::filesystem::path inputPath;
    std::filesystem::path outputPath;
    std::optional<format::DatasetProfile> profile;
    std::size_t memoryLimitBytes = kDefaultMemoryLimitBytes;
    std::uint64_t targetBlockBases = format::kMaxDefaultBlockBases;
    std::size_t maxBlockBytes = format::kMaxDefaultBlockBytes;
    int zstdLevel = format::kMaxDefaultZstdLevel;
    bool forceOverwrite = false;
};

/// Inputs for the experimental `fqc-max/v1` decompressor.
struct MaxDecompressionRequest {
    std::filesystem::path inputPath;
    std::filesystem::path outputPath;
    std::size_t memoryLimitBytes = kDefaultMemoryLimitBytes;
    bool forceOverwrite = false;
};

/// Experimental `fqc-max/v1` engine. It is intentionally separate from the
/// streaming v2 engine; the container and CLI are opt-in.
class MaxRatioEngine {
public:
    MaxRatioEngine() = default;

    /// Compress one FASTQ input into an `fqc-max/v1` archive.
    [[nodiscard]] static auto compress(const MaxCompressionRequest& request)
        -> Result<OperationStats>;
    /// Decode an `fqc-max/v1` archive into canonical LF FASTQ.
    [[nodiscard]] static auto decompress(const MaxDecompressionRequest& request)
        -> Result<OperationStats>;
    /// Decode every block and validate block/footer checksums without writing
    /// FASTQ.
    [[nodiscard]] static auto verify(const std::filesystem::path& inputPath,
                                     std::size_t memoryLimitBytes = kDefaultMemoryLimitBytes)
        -> Result<OperationStats>;
};

}  // namespace fqc::commands
