// =============================================================================
// fq-compressor - Phase 0 Ratio Probe
// =============================================================================
// Development-only measurement tool for the max-ratio track. It measures raw
// FASTQ streams without changing the production parser or archive format.
// =============================================================================

#pragma once

#include "fqc/common/error.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <string>
#include <string_view>

namespace fqc::tools {

/// Compression parameters used by the probe's reference baselines.
struct RatioProbeOptions {
    int zstdLevel = 19;
    int gzipLevel = 6;
};

/// Byte accounting for one logical FASTQ stream. `content` excludes the
/// line-ending bytes; `lineEndings` counts CR/LF bytes separately.
struct StreamBytes {
    std::uint64_t content = 0;
    std::uint64_t lineEndings = 0;

    [[nodiscard]] auto total() const noexcept -> std::uint64_t {
        return content + lineEndings;
    }
};

/// Raw, byte-preserving statistics for one FASTQ input.
///
/// Unlike `fqc::io::FastqParser`, this probe keeps `+` line content and CRLF
/// line endings and accepts empty sequences. It is intentionally a separate
/// scanner until the max-ratio format has its own byte-preserving parser.
struct RatioProbeStats {
    std::uint64_t totalBytes = 0;
    std::uint64_t records = 0;
    std::uint64_t lines = 0;
    std::uint64_t blankLines = 0;
    std::uint64_t bases = 0;
    std::uint64_t acgtBases = 0;
    std::uint64_t nBases = 0;
    std::uint64_t otherBases = 0;
    std::uint64_t emptySequences = 0;

    StreamBytes header;
    StreamBytes sequence;
    StreamBytes plus;
    StreamBytes quality;
    StreamBytes blank;

    std::uint64_t idBytes = 0;
    std::uint64_t commentBytes = 0;
    std::uint64_t plusEmpty = 0;
    std::uint64_t plusMatchesHeader = 0;
    std::uint64_t plusLiteral = 0;

    std::array<std::uint64_t, 256> qualityHistogram{};
    std::uint64_t invalidQualityBytes = 0;

    std::uint64_t packedMainBytes = 0;
    std::uint64_t packedExceptionBytes = 0;
    std::uint64_t packedSequenceBytes = 0;

    std::uint64_t lfLineEndings = 0;
    std::uint64_t crlfLineEndings = 0;
    std::uint64_t finalLineWithoutNewline = 0;

    std::uint64_t inputFileBytes = 0;
    bool inputFileBytesKnown = false;
    std::string inputPath;
    std::string inputCompression = "none";

    std::uint64_t gzipBytes = 0;
    std::uint64_t zstdBytes = 0;
    std::uint64_t idZstdBytes = 0;
    std::uint64_t qualityZstdBytes = 0;

    int gzipLevel = 6;
    int zstdLevel = 19;

    [[nodiscard]] auto qualitySymbols() const noexcept -> std::size_t;
    [[nodiscard]] auto validQualityBytes() const noexcept -> std::uint64_t;
    [[nodiscard]] auto meanQuality() const noexcept -> double;
};

/// Scan one FASTQ stream and accumulate statistics. The caller owns `stats`;
/// this function does not reset it, so input metadata can be filled first.
[[nodiscard]] auto analyzeFastq(std::istream& input,
                                const RatioProbeOptions& options,
                                RatioProbeStats& stats) -> VoidResult;

/// Open a plain/gzip FASTQ file (or "-" for stdin) and return its statistics.
[[nodiscard]] auto analyzeFile(const std::filesystem::path& path,
                               const RatioProbeOptions& options) -> Result<RatioProbeStats>;

/// Stable JSON representation for benchmark capture.
[[nodiscard]] auto toJson(const RatioProbeStats& stats) -> std::string;

/// Human-readable representation for quick inspection.
[[nodiscard]] auto toText(const RatioProbeStats& stats) -> std::string;

}  // namespace fqc::tools
