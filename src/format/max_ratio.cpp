// =============================================================================
// fq-compressor - Experimental fqc-max/v1 Archive
// =============================================================================

#include "fqc/format/max_ratio.h"

#include "fqc/format/quality_codec.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <istream>
#include <limits>
#include <ostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <xxhash.h>
#include <zstd.h>

namespace fqc::format {

namespace {

constexpr std::array<std::uint8_t, 8> kMaxMagic = {'F', 'Q', 'C', 'M', 'X', '1', '\r', '\n'};
constexpr std::uint32_t kBlockMagic = 0x424D5146U;   // "FQMB" little-endian
constexpr std::uint32_t kFooterMagic = 0x464D5146U;  // "FQMF" little-endian
constexpr std::uint16_t kGlobalHeaderSize = 32;
constexpr std::uint32_t kBlockHeaderSize = 88;
constexpr std::uint32_t kBlockHeaderChecksumTrailerSize = 12;
constexpr std::uint32_t kFooterSize = 40;
constexpr std::uint8_t kIdCodecZstd = 1;
constexpr std::uint8_t kSequenceCodecPackedZstd = 1;
constexpr std::uint8_t kHeaderQualityCodecMixed = 0;
constexpr std::uint8_t kBlockQualityCodecZstd = 0;
constexpr std::uint8_t kBlockQualityCodecFqz = 1;
constexpr std::size_t kCodecMemoryReserve = std::size_t{16} * 1024 * 1024;

using Bytes = std::vector<std::uint8_t>;

void appendU16(Bytes& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void appendU32(Bytes& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
    output.push_back(static_cast<std::uint8_t>(value >> 16U));
    output.push_back(static_cast<std::uint8_t>(value >> 24U));
}

void appendU64(Bytes& output, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        output.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

[[nodiscard]] auto checkedAdd(std::size_t& destination, std::size_t value) noexcept -> bool {
    if (value > std::numeric_limits<std::size_t>::max() - destination) {
        return false;
    }
    destination += value;
    return true;
}

[[nodiscard]] auto writeBytes(std::ostream& output,
                              std::span<const std::uint8_t> bytes) -> VoidResult {
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        return makeVoidError(ErrorCode::kIOError, "failed to write fqc-max/v1 archive");
    }
    return {};
}

[[nodiscard]] auto readExact(std::istream& input, std::span<std::uint8_t> bytes) -> VoidResult {
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
        if (input.bad()) {
            return makeVoidError(ErrorCode::kIOError, "I/O error while reading fqc-max/v1 archive");
        }
        return makeVoidError(ErrorCode::kFormatError, "truncated fqc-max/v1 archive");
    }
    return {};
}

class Cursor {
public:
    explicit Cursor(std::span<const std::uint8_t> data) : data_(data) {}

    [[nodiscard]] auto readU8() -> Result<std::uint8_t> {
        if (offset_ >= data_.size()) {
            return makeError<std::uint8_t>(ErrorCode::kFormatError, "truncated fqc-max/v1 field");
        }
        return data_[offset_++];
    }

    [[nodiscard]] auto readU16() -> Result<std::uint16_t> {
        auto bytes = readBytes(2);
        if (!bytes) {
            return makeError<std::uint16_t>(bytes.error());
        }
        return static_cast<std::uint16_t>((*bytes)[0]) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>((*bytes)[1]) << 8U);
    }

    [[nodiscard]] auto readU32() -> Result<std::uint32_t> {
        auto bytes = readBytes(4);
        if (!bytes) {
            return makeError<std::uint32_t>(bytes.error());
        }
        return static_cast<std::uint32_t>((*bytes)[0]) |
            (static_cast<std::uint32_t>((*bytes)[1]) << 8U) |
            (static_cast<std::uint32_t>((*bytes)[2]) << 16U) |
            (static_cast<std::uint32_t>((*bytes)[3]) << 24U);
    }

    [[nodiscard]] auto readU64() -> Result<std::uint64_t> {
        auto bytes = readBytes(8);
        if (!bytes) {
            return makeError<std::uint64_t>(bytes.error());
        }
        std::uint64_t result = 0;
        for (unsigned index = 0; index < 8; ++index) {
            result |= static_cast<std::uint64_t>((*bytes)[index]) << (index * 8U);
        }
        return result;
    }

    [[nodiscard]] auto readBytes(std::size_t size) -> Result<std::span<const std::uint8_t>> {
        if (size > data_.size() - offset_) {
            return makeError<std::span<const std::uint8_t>>(ErrorCode::kFormatError,
                                                            "truncated fqc-max/v1 field");
        }
        auto result = data_.subspan(offset_, size);
        offset_ += size;
        return result;
    }

private:
    std::span<const std::uint8_t> data_;
    std::size_t offset_ = 0;
};

[[nodiscard]] auto validProfile(std::uint8_t value) noexcept -> bool {
    return value >= static_cast<std::uint8_t>(DatasetProfile::kIllumina) &&
        value <= static_cast<std::uint8_t>(DatasetProfile::kPacBioClr);
}

[[nodiscard]] auto zstdError(std::size_t code) -> Error {
    return Error{ErrorCode::kInternalError, std::string("zstd: ") + ZSTD_getErrorName(code)};
}

[[nodiscard]] auto compressBytes(std::span<const std::uint8_t> input, int level) -> Result<Bytes> {
    Bytes output(ZSTD_compressBound(input.size()));
    const auto written =
        ZSTD_compress(output.data(), output.size(), input.data(), input.size(), level);
    if (ZSTD_isError(written) != 0U) {
        return makeError<Bytes>(zstdError(written));
    }
    output.resize(written);
    return output;
}

[[nodiscard]] auto decompressBytes(std::span<const std::uint8_t> input,
                                   std::size_t outputSize) -> Result<Bytes> {
    Bytes output(outputSize);
    const auto written = ZSTD_decompress(output.data(), output.size(), input.data(), input.size());
    if (ZSTD_isError(written) != 0U || written != outputSize) {
        return makeError<Bytes>(ErrorCode::kFormatError,
                                "fqc-max/v1 zstd stream failed to decompress");
    }
    return output;
}

}  // namespace

MaxArchiveWriter::MaxArchiveWriter(std::ostream& output, MaxArchiveOptions options)
    : output_(output),
      options_(options),
      metadata_{.version = kMaxArchiveVersion, .profile = options.profile} {}

auto MaxArchiveWriter::ensureHeader() -> VoidResult {
    if (headerWritten_) {
        return {};
    }
    if (options_.maxBlockBytes == 0 || options_.memoryLimitBytes <= kCodecMemoryReserve ||
        options_.zstdLevel < 1 || options_.zstdLevel > ZSTD_maxCLevel()) {
        return makeVoidError(ErrorCode::kUsageError, "invalid fqc-max/v1 writer options");
    }
    if (!validProfile(static_cast<std::uint8_t>(metadata_.profile))) {
        return makeVoidError(ErrorCode::kUsageError, "invalid fqc-max/v1 profile");
    }

    Bytes header;
    header.reserve(kGlobalHeaderSize);
    header.insert(header.end(), kMaxMagic.begin(), kMaxMagic.end());
    appendU16(header, kMaxArchiveVersion);
    appendU16(header, kGlobalHeaderSize);
    appendU32(header, 0);
    header.push_back(static_cast<std::uint8_t>(metadata_.profile));
    header.push_back(kIdCodecZstd);
    header.push_back(kSequenceCodecPackedZstd);
    header.push_back(kHeaderQualityCodecMixed);
    appendU32(header, 0);
    appendU64(header, XXH64(header.data(), header.size(), 0));
    auto result = writeBytes(output_, header);
    if (result) {
        headerWritten_ = true;
        stats_.encodedBytes += header.size();
    }
    return result;
}

auto MaxArchiveWriter::writeBlock(std::span<const ReadRecord> records) -> VoidResult {
    if (finished_) {
        return makeVoidError(ErrorCode::kFormatError, "fqc-max/v1 writer is already finished");
    }
    if (records.empty()) {
        return makeVoidError(ErrorCode::kUsageError, "fqc-max/v1 block cannot be empty");
    }
    FQC_TRY(ensureHeader());

    auto encoded = encodeFrame(records,
                               {.profile = options_.profile,
                                .paired = false,
                                .maxFrameBytes = options_.maxBlockBytes,
                                .memoryLimitBytes = options_.memoryLimitBytes,
                                .qualityZstdLevel = 1});
    if (!encoded) {
        return makeVoidError(encoded.error().code, encoded.error().message);
    }

    FQC_TRY_ASSIGN(ids, compressBytes((*encoded)->rawIds, options_.zstdLevel));
    FQC_TRY_ASSIGN(sequences, compressBytes((*encoded)->rawSequences, options_.zstdLevel));
    FQC_TRY_ASSIGN(zstdQualities, compressBytes((*encoded)->rawQualities, kMaxDefaultZstdLevel));

    auto fqz = compressFqzQuality(records);
    std::uint8_t qualityCodec = kBlockQualityCodecZstd;
    std::uint8_t qualityStrategy = 0;
    Bytes qualities = std::move(zstdQualities);
    if (fqz && fqz->bytes.size() < qualities.size()) {
        qualityCodec = kBlockQualityCodecFqz;
        qualityStrategy = static_cast<std::uint8_t>(fqz->strategy);
        qualities = std::move(fqz->bytes);
    }

    const auto rawQualitiesSize = (*encoded)->rawQualities.size();
    Bytes header;
    header.reserve(kBlockHeaderSize);
    appendU32(header, kBlockMagic);
    appendU32(header, kBlockHeaderSize);
    appendU32(header, static_cast<std::uint32_t>(stats_.blockCount));
    appendU32(header, (*encoded)->recordCount);
    appendU64(header, (*encoded)->rawIds.size());
    appendU64(header, ids.size());
    appendU64(header, (*encoded)->rawSequences.size());
    appendU64(header, sequences.size());
    appendU64(header, rawQualitiesSize);
    appendU64(header, qualities.size());
    appendU64(header, (*encoded)->checksum);
    header.push_back(qualityCodec);
    header.push_back(qualityStrategy);
    appendU16(header, 0);
    appendU64(header, XXH64(header.data(), header.size(), 0));
    appendU32(header, 0);

    for (const auto bytes : {std::span<const std::uint8_t>(header),
                             std::span<const std::uint8_t>(ids),
                             std::span<const std::uint8_t>(sequences),
                             std::span<const std::uint8_t>(qualities)}) {
        FQC_TRY(writeBytes(output_, bytes));
    }

    ++stats_.blockCount;
    stats_.recordCount += (*encoded)->recordCount;
    stats_.totalBases += (*encoded)->totalBases;
    stats_.encodedBytes += header.size() + ids.size() + sequences.size() + qualities.size();
    if (qualityCodec == kBlockQualityCodecFqz) {
        ++stats_.fqzBlocks;
    } else {
        ++stats_.zstdQualityBlocks;
    }
    globalChecksum_ = advanceGlobalChecksum(globalChecksum_, (*encoded)->checksum);
    return {};
}

auto MaxArchiveWriter::finish() -> VoidResult {
    if (finished_) {
        return {};
    }
    FQC_TRY(ensureHeader());
    Bytes footer;
    footer.reserve(kFooterSize);
    appendU32(footer, kFooterMagic);
    appendU32(footer, kFooterSize);
    appendU64(footer, stats_.blockCount);
    appendU64(footer, stats_.recordCount);
    appendU64(footer, stats_.totalBases);
    appendU64(footer, globalChecksum_);
    FQC_TRY(writeBytes(output_, footer));
    output_.flush();
    if (!output_) {
        return makeVoidError(ErrorCode::kIOError, "failed to flush fqc-max/v1 archive");
    }
    stats_.encodedBytes += footer.size();
    finished_ = true;
    return {};
}

MaxArchiveReader::MaxArchiveReader(std::istream& input,
                                   std::size_t maxBlockBytes,
                                   std::size_t memoryLimitBytes)
    : input_(input), maxBlockBytes_(maxBlockBytes), memoryLimitBytes_(memoryLimitBytes) {}

auto MaxArchiveReader::open() -> Result<MaxArchiveMetadata> {
    if (opened_) {
        return metadata_;
    }
    if (maxBlockBytes_ == 0 || memoryLimitBytes_ <= kCodecMemoryReserve) {
        return makeError<MaxArchiveMetadata>(ErrorCode::kUsageError,
                                             "invalid fqc-max/v1 reader limits");
    }
    std::array<std::uint8_t, 8> magicBytes{};
    FQC_TRY(readExact(input_, magicBytes));
    if (!std::equal(kMaxMagic.begin(), kMaxMagic.end(), magicBytes.begin())) {
        return makeError<MaxArchiveMetadata>(ErrorCode::kFormatError,
                                             "unknown magic (not an fqc-max/v1 archive)");
    }

    std::array<std::uint8_t, kGlobalHeaderSize> header{};
    std::copy(magicBytes.begin(), magicBytes.end(), header.begin());
    FQC_TRY(readExact(input_, std::span(header).subspan(magicBytes.size())));
    Cursor cursor(header);
    FQC_TRY_ASSIGN(magic, cursor.readBytes(kMaxMagic.size()));
    FQC_TRY_ASSIGN(version, cursor.readU16());
    FQC_TRY_ASSIGN(headerSize, cursor.readU16());
    FQC_TRY_ASSIGN(flags, cursor.readU32());
    FQC_TRY_ASSIGN(profile, cursor.readU8());
    FQC_TRY_ASSIGN(idCodec, cursor.readU8());
    FQC_TRY_ASSIGN(sequenceCodec, cursor.readU8());
    FQC_TRY_ASSIGN(qualityCodec, cursor.readU8());
    FQC_TRY_ASSIGN(reserved, cursor.readU32());
    FQC_TRY_ASSIGN(storedChecksum, cursor.readU64());
    (void)magic;

    if (version != kMaxArchiveVersion || headerSize != kGlobalHeaderSize) {
        return makeError<MaxArchiveMetadata>(ErrorCode::kUnsupportedCodec,
                                             "unsupported fqc-max/v1 version");
    }
    if (flags != 0 || reserved != 0U || !validProfile(profile) || idCodec != kIdCodecZstd ||
        sequenceCodec != kSequenceCodecPackedZstd || qualityCodec != kHeaderQualityCodecMixed) {
        return makeError<MaxArchiveMetadata>(ErrorCode::kUnsupportedCodec,
                                             "unsupported fqc-max/v1 header fields");
    }
    if (XXH64(header.data(), kGlobalHeaderSize - 8, 0) != storedChecksum) {
        return makeError<MaxArchiveMetadata>(ErrorCode::kChecksumError,
                                             "fqc-max/v1 header checksum mismatch");
    }
    metadata_ = {.version = version, .profile = static_cast<DatasetProfile>(profile)};
    stats_.encodedBytes = header.size();
    opened_ = true;
    return metadata_;
}

// 二进制块解析器；长度、magic、checksum 与内存预算校验集中在一处，拆分会引入
// 额外状态传递。保持单函数并显式豁免复杂度阈值。
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto MaxArchiveReader::readRawBlock() -> Result<std::optional<RawBlock>> {
    if (!opened_) {
        FQC_TRY(open());
    }
    if (finished_) {
        return std::optional<RawBlock>{};
    }

    std::array<std::uint8_t, 4> magicBytes{};
    FQC_TRY(readExact(input_, magicBytes));
    Cursor magicCursor(magicBytes);
    FQC_TRY_ASSIGN(magic, magicCursor.readU32());
    if (magic == kFooterMagic) {
        std::array<std::uint8_t, kFooterSize> footer{};
        std::copy(magicBytes.begin(), magicBytes.end(), footer.begin());
        FQC_TRY(readExact(input_, std::span(footer).subspan(4)));
        Cursor cursor(footer);
        FQC_TRY_ASSIGN(footerMagic, cursor.readU32());
        FQC_TRY_ASSIGN(size, cursor.readU32());
        FQC_TRY_ASSIGN(blockCount, cursor.readU64());
        FQC_TRY_ASSIGN(recordCount, cursor.readU64());
        FQC_TRY_ASSIGN(totalBases, cursor.readU64());
        FQC_TRY_ASSIGN(checksum, cursor.readU64());
        (void)footerMagic;
        if (size != kFooterSize) {
            return makeError<std::optional<RawBlock>>(ErrorCode::kFormatError,
                                                      "invalid fqc-max/v1 footer");
        }
        if (input_.peek() != std::char_traits<char>::eof()) {
            return makeError<std::optional<RawBlock>>(ErrorCode::kFormatError,
                                                      "trailing bytes after fqc-max/v1 footer");
        }
        if (input_.bad()) {
            return makeError<std::optional<RawBlock>>(ErrorCode::kIOError,
                                                      "failed while checking fqc-max/v1 footer");
        }
        const Footer parsed{.blockCount = blockCount,
                            .recordCount = recordCount,
                            .totalBases = totalBases,
                            .globalChecksum = checksum};
        FQC_TRY(validateFooter(parsed));
        footer_ = parsed;
        stats_.encodedBytes += footer.size();
        finished_ = true;
        return std::optional<RawBlock>{};
    }
    if (magic != kBlockMagic) {
        return makeError<std::optional<RawBlock>>(ErrorCode::kFormatError,
                                                  "invalid fqc-max/v1 block marker");
    }

    std::array<std::uint8_t, kBlockHeaderSize> header{};
    std::copy(magicBytes.begin(), magicBytes.end(), header.begin());
    FQC_TRY(readExact(input_, std::span(header).subspan(4)));
    Cursor cursor(header);
    FQC_TRY_ASSIGN(marker, cursor.readU32());
    FQC_TRY_ASSIGN(headerSize, cursor.readU32());
    FQC_TRY_ASSIGN(blockId, cursor.readU32());
    FQC_TRY_ASSIGN(recordCount, cursor.readU32());
    FQC_TRY_ASSIGN(rawIdsSize, cursor.readU64());
    FQC_TRY_ASSIGN(idsSize, cursor.readU64());
    FQC_TRY_ASSIGN(rawSequencesSize, cursor.readU64());
    FQC_TRY_ASSIGN(sequencesSize, cursor.readU64());
    FQC_TRY_ASSIGN(rawQualitiesSize, cursor.readU64());
    FQC_TRY_ASSIGN(qualitiesSize, cursor.readU64());
    FQC_TRY_ASSIGN(storedChecksum, cursor.readU64());
    FQC_TRY_ASSIGN(qualityCodec, cursor.readU8());
    FQC_TRY_ASSIGN(qualityStrategy, cursor.readU8());
    FQC_TRY_ASSIGN(reserved, cursor.readU16());
    FQC_TRY_ASSIGN(headerChecksum, cursor.readU64());
    (void)marker;
    (void)reserved;

    if (headerSize != kBlockHeaderSize || blockId != stats_.blockCount || recordCount == 0 ||
        qualityCodec > kBlockQualityCodecFqz || qualityStrategy > 3 ||
        XXH64(header.data(), kBlockHeaderSize - kBlockHeaderChecksumTrailerSize, 0) !=
            headerChecksum) {
        return makeError<std::optional<RawBlock>>(ErrorCode::kFormatError,
                                                  "invalid fqc-max/v1 block header");
    }
    for (const auto size :
         {rawIdsSize, idsSize, rawSequencesSize, sequencesSize, rawQualitiesSize, qualitiesSize}) {
        if (size > maxBlockBytes_ || size > std::numeric_limits<std::size_t>::max()) {
            return makeError<std::optional<RawBlock>>(ErrorCode::kFormatError,
                                                      "fqc-max/v1 block exceeds size limit");
        }
    }

    std::size_t peak = kCodecMemoryReserve;
    for (const auto size :
         {rawIdsSize, idsSize, rawSequencesSize, sequencesSize, rawQualitiesSize, qualitiesSize}) {
        if (!checkedAdd(peak, static_cast<std::size_t>(size))) {
            return makeError<std::optional<RawBlock>>(ErrorCode::kFormatError,
                                                      "fqc-max/v1 memory estimate overflow");
        }
    }
    if (!checkedAdd(peak, static_cast<std::size_t>(recordCount) * sizeof(ReadRecord) * 2)) {
        return makeError<std::optional<RawBlock>>(ErrorCode::kFormatError,
                                                  "fqc-max/v1 memory estimate overflow");
    }
    if (peak > memoryLimitBytes_) {
        return makeError<std::optional<RawBlock>>(ErrorCode::kFormatError,
                                                  "fqc-max/v1 block exceeds memory limit");
    }

    RawBlock block;
    block.rawIdsSize = static_cast<std::size_t>(rawIdsSize);
    block.rawSequencesSize = static_cast<std::size_t>(rawSequencesSize);
    block.rawQualitiesSize = static_cast<std::size_t>(rawQualitiesSize);
    block.recordCount = recordCount;
    block.qualityCodec = qualityCodec;
    block.qualityStrategy = qualityStrategy;
    block.checksum = storedChecksum;
    block.ids.resize(static_cast<std::size_t>(idsSize));
    block.sequences.resize(static_cast<std::size_t>(sequencesSize));
    block.qualities.resize(static_cast<std::size_t>(qualitiesSize));
    for (const auto payload :
         {std::span(block.ids), std::span(block.sequences), std::span(block.qualities)}) {
        FQC_TRY(readExact(input_, payload));
    }
    stats_.encodedBytes +=
        kBlockHeaderSize + block.ids.size() + block.sequences.size() + block.qualities.size();
    return std::optional<RawBlock>(std::move(block));
}

auto MaxArchiveReader::decodeBlock(const RawBlock& block) -> Result<std::vector<ReadRecord>> {
    FQC_TRY_ASSIGN(ids, decompressBytes(block.ids, block.rawIdsSize));
    FQC_TRY_ASSIGN(sequences, decompressBytes(block.sequences, block.rawSequencesSize));
    Bytes qualities;
    if (block.qualityCodec == kBlockQualityCodecFqz) {
        FQC_TRY_ASSIGN(decodedQualities,
                       decompressFqzQuality(block.qualities, block.recordCount, maxBlockBytes_));
        qualities = std::move(decodedQualities);
    } else {
        FQC_TRY_ASSIGN(decodedQualities, decompressBytes(block.qualities, block.rawQualitiesSize));
        qualities = std::move(decodedQualities);
    }

    if (logicalChecksum(ids, sequences, qualities) != block.checksum) {
        return makeError<std::vector<ReadRecord>>(ErrorCode::kChecksumError,
                                                  "fqc-max/v1 block checksum mismatch");
    }
    FQC_TRY_ASSIGN(records, decodeLogicalStreams(ids, sequences, qualities, block.recordCount));
    std::uint64_t totalBases = 0;
    for (const auto& record : records) {
        totalBases += record.sequence.size();
    }
    stats_.recordCount += block.recordCount;
    stats_.totalBases += totalBases;
    if (block.qualityCodec == kBlockQualityCodecFqz) {
        ++stats_.fqzBlocks;
    } else {
        ++stats_.zstdQualityBlocks;
    }
    globalChecksum_ = advanceGlobalChecksum(globalChecksum_, block.checksum);
    return records;
}

auto MaxArchiveReader::readBlock() -> Result<std::optional<std::vector<ReadRecord>>> {
    auto raw = readRawBlock();
    if (!raw) {
        return makeError<std::optional<std::vector<ReadRecord>>>(raw.error());
    }
    if (!raw->has_value()) {
        return std::optional<std::vector<ReadRecord>>{};
    }
    // raw 已由 has_value() 保证 engaged；见 archive.cpp 的同类检查器注释。
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    auto decoded = decodeBlock(**raw);
    if (!decoded) {
        return makeError<std::optional<std::vector<ReadRecord>>>(decoded.error());
    }
    ++stats_.blockCount;
    return std::optional<std::vector<ReadRecord>>(std::move(*decoded));
}

auto MaxArchiveReader::validateFooter(const Footer& footer) const -> VoidResult {
    if (footer.blockCount != stats_.blockCount || footer.recordCount != stats_.recordCount ||
        footer.totalBases != stats_.totalBases) {
        return makeVoidError(ErrorCode::kFormatError, "fqc-max/v1 footer totals disagree");
    }
    if (footer.globalChecksum != globalChecksum_) {
        return makeVoidError(ErrorCode::kChecksumError, "fqc-max/v1 global checksum mismatch");
    }
    return {};
}

}  // namespace fqc::format
