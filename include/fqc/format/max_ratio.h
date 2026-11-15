// =============================================================================
// fq-compressor - Experimental fqc-max/v1 Archive
// =============================================================================

#pragma once

#include "fqc/common/error.h"
#include "fqc/common/types.h"
#include "fqc/format/archive.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <span>
#include <vector>

namespace fqc::format {

inline constexpr std::uint16_t kMaxArchiveVersion = 1;
inline constexpr std::uint64_t kMaxDefaultBlockBases = 4'000'000;
inline constexpr std::size_t kMaxDefaultBlockBytes = std::size_t{128} * 1024 * 1024;
inline constexpr int kMaxDefaultZstdLevel = 19;

struct MaxArchiveOptions {
    DatasetProfile profile = DatasetProfile::kIllumina;
    /// Raw block ceiling, enforced by the shared v2 stream encoder.
    std::size_t maxBlockBytes = kMaxDefaultBlockBytes;
    std::size_t memoryLimitBytes = kDefaultMemoryLimitBytes;
    /// zstd level for ID and packed-sequence streams. Quality uses fqzcomp
    /// when it wins and falls back to zstd level 19 per block.
    int zstdLevel = kMaxDefaultZstdLevel;
};

struct MaxArchiveMetadata {
    std::uint16_t version = kMaxArchiveVersion;
    DatasetProfile profile = DatasetProfile::kIllumina;
};

struct MaxArchiveStats {
    std::uint64_t blockCount = 0;
    std::uint64_t recordCount = 0;
    std::uint64_t totalBases = 0;
    std::uint64_t encodedBytes = 0;
    std::uint64_t fqzBlocks = 0;
    std::uint64_t zstdQualityBlocks = 0;
};

/// Experimental max-ratio writer. It deliberately reuses the v2 logical
/// ID/packed-sequence stream encoding but swaps the quality payload for
/// htscodecs fqzcomp (with a per-block zstd fallback). The container magic,
/// frame headers and footer are independent from `fqc-sequential/v2`.
///
/// This is a Phase 1 prototype: it preserves canonical LF FASTQ records and
/// current production validation rules, not the future byte-preserving
/// max-ratio metadata boundary (`+` line contents, mixed CRLF, empty reads).
class MaxArchiveWriter {
public:
    MaxArchiveWriter(std::ostream& output, MaxArchiveOptions options);

    MaxArchiveWriter(const MaxArchiveWriter&) = delete;
    MaxArchiveWriter& operator=(const MaxArchiveWriter&) = delete;
    MaxArchiveWriter(MaxArchiveWriter&&) = delete;
    MaxArchiveWriter& operator=(MaxArchiveWriter&&) = delete;

    [[nodiscard]] auto writeBlock(std::span<const ReadRecord> records) -> VoidResult;
    [[nodiscard]] auto finish() -> VoidResult;

    [[nodiscard]] auto metadata() const noexcept -> const MaxArchiveMetadata& {
        return metadata_;
    }

    [[nodiscard]] auto stats() const noexcept -> const MaxArchiveStats& {
        return stats_;
    }

private:
    [[nodiscard]] auto ensureHeader() -> VoidResult;

    std::ostream& output_;
    MaxArchiveOptions options_;
    MaxArchiveMetadata metadata_;
    MaxArchiveStats stats_;
    std::uint64_t globalChecksum_ = 0;
    bool headerWritten_ = false;
    bool finished_ = false;
};

/// Sequential reader for `fqc-max/v1`. It decodes one block at a time; the
/// quality codec is selected per block in the block header.
class MaxArchiveReader {
public:
    explicit MaxArchiveReader(std::istream& input,
                              std::size_t maxBlockBytes = kMaxDefaultBlockBytes,
                              std::size_t memoryLimitBytes = kDefaultMemoryLimitBytes);

    MaxArchiveReader(const MaxArchiveReader&) = delete;
    MaxArchiveReader& operator=(const MaxArchiveReader&) = delete;
    MaxArchiveReader(MaxArchiveReader&&) = delete;
    MaxArchiveReader& operator=(MaxArchiveReader&&) = delete;

    [[nodiscard]] auto open() -> Result<MaxArchiveMetadata>;

    /// Return the next decoded block, or nullopt after a validated footer.
    [[nodiscard]] auto readBlock() -> Result<std::optional<std::vector<ReadRecord>>>;

    [[nodiscard]] auto metadata() const noexcept -> const MaxArchiveMetadata& {
        return metadata_;
    }

    [[nodiscard]] auto stats() const noexcept -> const MaxArchiveStats& {
        return stats_;
    }

private:
    struct RawBlock {
        std::vector<std::uint8_t> ids;
        std::vector<std::uint8_t> sequences;
        std::vector<std::uint8_t> qualities;
        std::size_t rawIdsSize = 0;
        std::size_t rawSequencesSize = 0;
        std::size_t rawQualitiesSize = 0;
        std::uint32_t recordCount = 0;
        std::uint8_t qualityCodec = 0;
        std::uint8_t qualityStrategy = 0;
        std::uint64_t checksum = 0;
    };

    struct Footer {
        std::uint64_t blockCount = 0;
        std::uint64_t recordCount = 0;
        std::uint64_t totalBases = 0;
        std::uint64_t globalChecksum = 0;
    };

    [[nodiscard]] auto readRawBlock() -> Result<std::optional<RawBlock>>;
    [[nodiscard]] auto decodeBlock(const RawBlock& block) -> Result<std::vector<ReadRecord>>;
    [[nodiscard]] auto validateFooter(const Footer& footer) const -> VoidResult;

    std::istream& input_;
    std::size_t maxBlockBytes_;
    std::size_t memoryLimitBytes_;
    MaxArchiveMetadata metadata_;
    MaxArchiveStats stats_;
    std::optional<Footer> footer_;
    std::uint64_t globalChecksum_ = 0;
    bool opened_ = false;
    bool finished_ = false;
};

}  // namespace fqc::format
