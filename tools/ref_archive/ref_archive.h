// =============================================================================
// fq-compressor - Reference-Backed Experimental Archive
// =============================================================================

#pragma once

#include "fqc/common/error.h"

#include <cstdint>
#include <filesystem>

namespace fqc::tools::ref {

/// Inputs for the PAF-sidecar `fqc-max-ref/v1` prototype. The reference is
/// external; only its digest and contig table are stored in the archive.
struct RefCompressionRequest {
    std::filesystem::path inputPath;
    std::filesystem::path pafPath;
    std::filesystem::path referencePath;
    std::filesystem::path outputPath;
    std::uint64_t blockBases = 4'000'000;
    std::uint32_t maxMismatches = 16;
    bool forceOverwrite = false;
};

/// Counters shared by compress/decompress/verify.
struct RefStats {
    std::uint64_t blocks = 0;
    std::uint64_t records = 0;
    std::uint64_t bases = 0;
    std::uint64_t mapped = 0;
    std::uint64_t literal = 0;
    std::uint64_t clipped = 0;
    std::uint64_t indel = 0;
    std::uint64_t idsBytes = 0;
    std::uint64_t qualitiesBytes = 0;
    std::uint64_t alignmentsBytes = 0;
    std::uint64_t fqzBlocks = 0;
    std::uint64_t zstdQualityBlocks = 0;
    std::uint64_t frontIdBlocks = 0;
    std::uint64_t inputBytes = 0;
    std::uint64_t outputBytes = 0;
};

/// Compress FASTQ with a PAF sidecar and an external reference FASTA.
[[nodiscard]] auto compress(const RefCompressionRequest& request) -> Result<RefStats>;

/// Decode an `fqc-max-ref/v1` archive. The same reference is required.
[[nodiscard]] auto decompress(const std::filesystem::path& archivePath,
                              const std::filesystem::path& referencePath,
                              const std::filesystem::path& outputPath,
                              bool forceOverwrite = false) -> Result<RefStats>;

/// Decode every block and validate checksums without writing FASTQ.
[[nodiscard]] auto verify(const std::filesystem::path& archivePath,
                          const std::filesystem::path& referencePath) -> Result<RefStats>;

}  // namespace fqc::tools::ref
