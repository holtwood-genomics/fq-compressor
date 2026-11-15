// =============================================================================
// fq-compressor - Experimental Max-Ratio Engine
// =============================================================================

#include "fqc/commands/max_ratio_engine.h"

#include "fqc/commands/profile.h"
#include "fqc/common/error.h"
#include "fqc/common/types.h"
#include "fqc/io/compressed_stream.h"
#include "fqc/io/fastq_parser.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fqc::commands {

namespace {

constexpr std::size_t kMaxEngineReserveBytes = std::size_t{16} * 1024 * 1024;
constexpr std::size_t kMaxMinimumMemoryBytes = std::size_t{64} * 1024 * 1024;

class MaxOutputTransaction {
public:
    MaxOutputTransaction(std::filesystem::path finalPath, bool useStaging)
        : finalPath_(std::move(finalPath)), stagingPath_(finalPath_) {
        if (useStaging && finalPath_ != "-") {
            static std::atomic<std::uint64_t> sequence = 0;
            const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
            stagingPath_ += ".tmp." + std::to_string(timestamp) + "." +
                std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
        }
    }

    ~MaxOutputTransaction() {
        if (!committed_ && stagingPath_ != finalPath_) {
            std::error_code ignored;
            std::filesystem::remove(stagingPath_, ignored);
        }
    }

    MaxOutputTransaction(const MaxOutputTransaction&) = delete;
    auto operator=(const MaxOutputTransaction&) -> MaxOutputTransaction& = delete;
    MaxOutputTransaction(MaxOutputTransaction&&) = delete;
    auto operator=(MaxOutputTransaction&&) -> MaxOutputTransaction& = delete;

    [[nodiscard]] auto path() const noexcept -> const std::filesystem::path& {
        return stagingPath_;
    }

    [[nodiscard]] auto commit(bool forceOverwrite) -> VoidResult {
        if (stagingPath_ == finalPath_) {
            committed_ = true;
            return {};
        }
        std::error_code error;
        const auto outputExists = std::filesystem::exists(finalPath_, error);
        if (error) {
            return makeVoidError(ErrorCode::kIOError,
                                 "failed to inspect output path: " + error.message());
        }
        if (outputExists && !forceOverwrite) {
            return makeVoidError(ErrorCode::kIOError,
                                 "output already exists: " + finalPath_.string());
        }
        std::filesystem::rename(stagingPath_, finalPath_, error);
        if (error && forceOverwrite && outputExists) {
            error.clear();
            std::filesystem::remove(finalPath_, error);
            if (!error) {
                std::filesystem::rename(stagingPath_, finalPath_, error);
            }
        }
        if (error) {
            return makeVoidError(ErrorCode::kIOError,
                                 "failed to publish output: " + error.message());
        }
        committed_ = true;
        return {};
    }

private:
    std::filesystem::path finalPath_;
    std::filesystem::path stagingPath_;
    bool committed_ = false;
};

[[nodiscard]] auto validateMemoryLimit(std::size_t memoryLimitBytes) -> VoidResult {
    if (memoryLimitBytes < kMaxMinimumMemoryBytes) {
        return makeVoidError(ErrorCode::kUsageError, "memory limit must be at least 64 MiB");
    }
    return {};
}

[[nodiscard]] auto maxBlockBytesFor(std::size_t memoryLimitBytes) noexcept -> std::size_t {
    return std::min(format::kMaxDefaultBlockBytes, (memoryLimitBytes - kMaxEngineReserveBytes) / 8);
}

[[nodiscard]] auto validateOutput(const std::filesystem::path& inputPath,
                                  const std::filesystem::path& outputPath,
                                  bool forceOverwrite) -> VoidResult {
    if (inputPath.empty() || outputPath.empty()) {
        return makeVoidError(ErrorCode::kUsageError, "input and output paths are required");
    }
    if (inputPath != "-" && outputPath != "-" && inputPath == outputPath) {
        return makeVoidError(ErrorCode::kUsageError, "input and output paths must differ");
    }
    if (!forceOverwrite && outputPath != "-" && std::filesystem::exists(outputPath)) {
        return makeVoidError(ErrorCode::kIOError, "output already exists: " + outputPath.string());
    }
    return {};
}

[[nodiscard]] auto openOutput(const std::filesystem::path& path)
    -> Result<std::unique_ptr<std::ostream>> {
    if (path == "-") {
        return std::unique_ptr<std::ostream>(std::make_unique<std::ostream>(std::cout.rdbuf()));
    }
    auto stream = std::make_unique<std::ofstream>(path, std::ios::binary);
    if (!stream->is_open()) {
        return makeError<std::unique_ptr<std::ostream>>(
            ErrorCode::kIOError, "cannot create output file: " + path.string());
    }
    return std::unique_ptr<std::ostream>(std::move(stream));
}

[[nodiscard]] auto writeFastqRecord(std::ostream& output, const ReadRecord& record) -> VoidResult {
    output << '@' << record.id;
    if (!record.comment.empty()) {
        output << record.comment;
    }
    output << '\n' << record.sequence << "\n+\n" << record.quality << '\n';
    if (!output) {
        return makeVoidError(ErrorCode::kIOError, "failed to write decompressed FASTQ");
    }
    return {};
}

[[nodiscard]] auto toOperationStats(const format::MaxArchiveMetadata& metadata,
                                    const format::MaxArchiveStats& archiveStats,
                                    bool compressing,
                                    std::uint64_t logicalBytes) -> OperationStats {
    return {
        .profile = metadata.profile,
        .paired = false,
        .frameCount = archiveStats.blockCount,
        .recordCount = archiveStats.recordCount,
        .totalBases = archiveStats.totalBases,
        .inputBytes = compressing ? logicalBytes : archiveStats.encodedBytes,
        .outputBytes = compressing ? archiveStats.encodedBytes : logicalBytes,
    };
}

[[nodiscard]] auto readNextBlock(io::FastqParser& parser,
                                 std::uint64_t targetBlockBases,
                                 std::uint64_t& logicalBytes) -> Result<std::vector<ReadRecord>> {
    std::vector<ReadRecord> block;
    std::uint64_t bases = 0;
    while (bases < targetBlockBases) {
        auto record = parser.readRecord();
        if (!record) {
            return makeError<std::vector<ReadRecord>>(record.error());
        }
        if (!record->has_value()) {
            break;
        }
        bases += (*record)->sequence.size();
        logicalBytes += canonicalFastqBytes(**record);
        block.push_back(std::move(**record));
    }
    return block;
}

}  // namespace

auto MaxRatioEngine::compress(const MaxCompressionRequest& request) -> Result<OperationStats> {
    FQC_TRY(validateMemoryLimit(request.memoryLimitBytes));
    if (request.targetBlockBases == 0 || request.maxBlockBytes == 0) {
        return makeError<OperationStats>(ErrorCode::kUsageError, "block size must be positive");
    }
    FQC_TRY(validateOutput(request.inputPath, request.outputPath, request.forceOverwrite));
    FQC_TRY_ASSIGN(input, io::openInputFile(request.inputPath));
    io::FastqParser parser(*input);

    std::uint64_t logicalBytes = 0;
    auto firstBlock = readNextBlock(parser, request.targetBlockBases, logicalBytes);
    if (!firstBlock) {
        return makeError<OperationStats>(firstBlock.error());
    }

    format::DatasetProfile profile = format::DatasetProfile::kIllumina;
    if (!firstBlock->empty()) {
        if (request.profile.has_value()) {
            profile = *request.profile;
        } else {
            auto detected = detectProfile(*firstBlock);
            if (!detected) {
                return makeError<OperationStats>(detected.error());
            }
            profile = *detected;
        }
    }

    MaxOutputTransaction outputTransaction(request.outputPath, request.outputPath != "-");
    FQC_TRY_ASSIGN(output, openOutput(outputTransaction.path()));
    format::MaxArchiveWriter writer(
        *output,
        {.profile = profile,
         .maxBlockBytes =
             std::min(request.maxBlockBytes, maxBlockBytesFor(request.memoryLimitBytes)),
         .memoryLimitBytes = request.memoryLimitBytes,
         .zstdLevel = request.zstdLevel});

    if (!firstBlock->empty()) {
        FQC_TRY(writer.writeBlock(*firstBlock));
    }
    for (;;) {
        auto block = readNextBlock(parser, request.targetBlockBases, logicalBytes);
        if (!block) {
            return makeError<OperationStats>(block.error());
        }
        if (block->empty()) {
            break;
        }
        FQC_TRY(writer.writeBlock(*block));
    }
    FQC_TRY(writer.finish());
    output.reset();
    FQC_TRY(outputTransaction.commit(request.forceOverwrite));
    return toOperationStats(writer.metadata(), writer.stats(), true, logicalBytes);
}

auto MaxRatioEngine::decompress(const MaxDecompressionRequest& request) -> Result<OperationStats> {
    FQC_TRY(validateMemoryLimit(request.memoryLimitBytes));
    FQC_TRY(validateOutput(request.inputPath, request.outputPath, request.forceOverwrite));
    FQC_TRY_ASSIGN(input, io::openInputFile(request.inputPath));
    MaxOutputTransaction outputTransaction(request.outputPath, request.outputPath != "-");
    FQC_TRY_ASSIGN(output, openOutput(outputTransaction.path()));

    format::MaxArchiveReader reader(
        *input, maxBlockBytesFor(request.memoryLimitBytes), request.memoryLimitBytes);
    FQC_TRY(reader.open());
    std::uint64_t logicalBytes = 0;
    for (;;) {
        auto block = reader.readBlock();
        if (!block) {
            return makeError<OperationStats>(block.error());
        }
        if (!block->has_value()) {
            break;
        }
        for (const auto& record : **block) {
            logicalBytes += canonicalFastqBytes(record);
            FQC_TRY(writeFastqRecord(*output, record));
        }
    }
    output->flush();
    if (!*output) {
        return makeError<OperationStats>(ErrorCode::kIOError, "failed to flush decompressed FASTQ");
    }
    output.reset();
    FQC_TRY(outputTransaction.commit(request.forceOverwrite));
    return toOperationStats(reader.metadata(), reader.stats(), false, logicalBytes);
}

auto MaxRatioEngine::verify(const std::filesystem::path& inputPath,
                            std::size_t memoryLimitBytes) -> Result<OperationStats> {
    FQC_TRY(validateMemoryLimit(memoryLimitBytes));
    FQC_TRY_ASSIGN(input, io::openInputFile(inputPath));
    format::MaxArchiveReader reader(*input, maxBlockBytesFor(memoryLimitBytes), memoryLimitBytes);
    FQC_TRY(reader.open());
    std::uint64_t logicalBytes = 0;
    for (;;) {
        auto block = reader.readBlock();
        if (!block) {
            return makeError<OperationStats>(block.error());
        }
        if (!block->has_value()) {
            break;
        }
        for (const auto& record : **block) {
            logicalBytes += canonicalFastqBytes(record);
        }
    }
    return toOperationStats(reader.metadata(), reader.stats(), false, logicalBytes);
}

}  // namespace fqc::commands
