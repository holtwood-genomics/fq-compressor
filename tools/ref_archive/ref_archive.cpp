// =============================================================================
// fq-compressor - Reference-Backed Experimental Archive
// =============================================================================

#include "ref_archive.h"

#include "fqc/common/types.h"
#include "fqc/format/archive.h"
#include "fqc/format/quality_codec.h"
#include "fqc/io/fastq_parser.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <xxhash.h>
#include <zstd.h>

namespace fqc::tools::ref {

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::array<std::uint8_t, 8> kMagic = {'F', 'Q', 'C', 'M', 'R', '1', '\r', '\n'};
constexpr std::uint32_t kBlockMagic = 0x524D5146U;   // "FQMR"
constexpr std::uint32_t kFooterMagic = 0x46525146U;  // "FQRF"
constexpr std::uint16_t kVersion = 1;
constexpr std::uint16_t kGlobalHeaderSize = 52;
constexpr std::size_t kBlockHeaderSize = 88;
constexpr std::size_t kFooterSize = 40;
constexpr std::size_t kMaxHeaderTableBytes = std::size_t{16} * 1024 * 1024;
constexpr std::size_t kMaxStreamBytes = std::size_t{512} * 1024 * 1024;
constexpr int kZstdLevel = 19;
constexpr std::uint8_t kIdCodecRawZstd = 1;
constexpr std::uint8_t kIdCodecFrontZstd = 2;
constexpr std::uint8_t kReadFlagMapped = 0x01;
constexpr std::uint8_t kReadFlagReverse = 0x02;
constexpr std::uint8_t kReadFlagLiteral = 0x04;
constexpr std::uint8_t kReadFlagExtended = 0x08;
constexpr std::size_t kMaxCigarOps = 256;

struct Contig {
    std::string name;
    std::string sequence;
};

struct Reference {
    std::vector<Contig> contigs;
    std::unordered_map<std::string, std::uint32_t> index;
    std::uint64_t bases = 0;
    std::uint64_t digest = 0;
};

struct GlobalHeader {
    std::vector<std::pair<std::string, std::uint64_t>> contigs;
    std::uint64_t referenceBases = 0;
    std::uint64_t digest = 0;
};

struct CigarOp {
    std::uint32_t length = 0;
    std::uint8_t code = 0;  // 0=M/=, 1=I, 2=D
};

struct PafAlignment {
    std::string target;
    std::uint64_t queryLength = 0;
    std::uint64_t targetStart = 0;
    std::uint64_t targetEnd = 0;
    std::uint64_t queryStart = 0;
    std::uint64_t queryEnd = 0;
    char strand = '+';
    std::uint32_t mismatches = 0;
    std::string cigar;
    std::uint32_t mapq = 0;
};

struct EncodedRead {
    std::uint8_t flags = kReadFlagLiteral;
    std::uint64_t contigId = 0;
    std::uint64_t targetStart = 0;
    std::uint64_t readLen = 0;
    std::uint64_t queryStart = 0;
    std::uint64_t queryEnd = 0;
    std::uint32_t mismatches = 0;
    std::vector<CigarOp> cigar;
    std::vector<std::uint8_t> insertionBases;
    std::string leftClip;
    std::string rightClip;
    std::vector<std::pair<std::uint32_t, std::uint8_t>> edits;
    std::string literal;
};

struct BlockStats {
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
};

struct DecodedBlock {
    std::vector<ReadRecord> records;
    BlockStats stats;
};

class Cursor {
public:
    explicit Cursor(std::span<const std::uint8_t> data) : data_(data) {}

    [[nodiscard]] auto readU8() -> Result<std::uint8_t> {
        if (offset_ >= data_.size()) {
            return makeError<std::uint8_t>(ErrorCode::kFormatError, "truncated field");
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
        std::uint64_t value = 0;
        for (unsigned i = 0; i < 8; ++i) {
            value |= static_cast<std::uint64_t>((*bytes)[i]) << (i * 8U);
        }
        return value;
    }

    [[nodiscard]] auto readVarint() -> Result<std::uint64_t> {
        std::uint64_t value = 0;
        for (unsigned shift = 0; shift < 64; shift += 7) {
            auto byte = readU8();
            if (!byte) {
                return makeError<std::uint64_t>(byte.error());
            }
            value |= static_cast<std::uint64_t>(*byte & 0x7FU) << shift;
            if ((*byte & 0x80U) == 0U) {
                return value;
            }
        }
        return makeError<std::uint64_t>(ErrorCode::kFormatError, "varint overflows 64 bits");
    }

    [[nodiscard]] auto readBytes(std::size_t count) -> Result<std::span<const std::uint8_t>> {
        if (count > data_.size() - offset_) {
            return makeError<std::span<const std::uint8_t>>(ErrorCode::kFormatError,
                                                            "truncated field");
        }
        auto slice = data_.subspan(offset_, count);
        offset_ += count;
        return slice;
    }

    [[nodiscard]] auto remaining() const noexcept -> std::size_t {
        return data_.size() - offset_;
    }

private:
    std::span<const std::uint8_t> data_;
    std::size_t offset_ = 0;
};

void appendU8(Bytes& output, std::uint8_t value) {
    output.push_back(value);
}

void appendU16(Bytes& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void appendU32(Bytes& output, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        output.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void appendU64(Bytes& output, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        output.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

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

[[nodiscard]] auto readExact(std::istream& input, std::span<std::uint8_t> bytes) -> VoidResult {
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
        if (input.bad()) {
            return makeVoidError(ErrorCode::kIOError, "I/O error while reading archive");
        }
        return makeVoidError(ErrorCode::kFormatError, "truncated archive");
    }
    return {};
}

[[nodiscard]] auto writeBytes(std::ostream& output,
                              std::span<const std::uint8_t> bytes) -> VoidResult {
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        return makeVoidError(ErrorCode::kIOError, "failed to write archive");
    }
    return {};
}

[[nodiscard]] auto zstdCompress(std::span<const std::uint8_t> input, int level) -> Result<Bytes> {
    Bytes output(ZSTD_compressBound(input.size()));
    const auto written =
        ZSTD_compress(output.data(), output.size(), input.data(), input.size(), level);
    if (ZSTD_isError(written) != 0U) {
        return makeError<Bytes>(ErrorCode::kInternalError, "zstd compression failed");
    }
    output.resize(written);
    return output;
}

[[nodiscard]] auto zstdDecompress(std::span<const std::uint8_t> input,
                                  std::size_t rawSize) -> Result<Bytes> {
    Bytes output(rawSize);
    const auto written = ZSTD_decompress(output.data(), output.size(), input.data(), input.size());
    if (ZSTD_isError(written) != 0U || written != rawSize) {
        return makeError<Bytes>(ErrorCode::kFormatError, "zstd stream failed to decompress");
    }
    return output;
}

[[nodiscard]] auto digestReference(const Reference& reference) -> std::uint64_t {
    auto* state = XXH64_createState();
    XXH64_reset(state, 0);
    for (const auto& contig : reference.contigs) {
        XXH64_update(state, contig.name.data(), contig.name.size());
        const std::uint8_t separator = 0;
        XXH64_update(state, &separator, 1);
        XXH64_update(state, contig.sequence.data(), contig.sequence.size());
        XXH64_update(state, &separator, 1);
        const auto length = static_cast<std::uint64_t>(contig.sequence.size());
        XXH64_update(state, &length, sizeof(length));
    }
    const auto digest = XXH64_digest(state);
    XXH64_freeState(state);
    return digest;
}

[[nodiscard]] auto loadReference(const std::filesystem::path& path) -> Result<Reference> {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return makeError<Reference>(ErrorCode::kIOError,
                                    "failed to open reference: " + path.string());
    }

    Reference reference;
    std::string line;
    Contig* current = nullptr;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        if (line.front() == '>') {
            auto name = line.substr(1);
            const auto separator = name.find_first_of(" \t");
            if (separator != std::string::npos) {
                name.resize(separator);
            }
            reference.contigs.push_back({std::move(name), {}});
            current = &reference.contigs.back();
            continue;
        }
        if (current == nullptr) {
            return makeError<Reference>(ErrorCode::kFormatError,
                                        "reference sequence before first header");
        }
        current->sequence += line;
    }
    if (!input.eof()) {
        return makeError<Reference>(ErrorCode::kIOError, "failed to read reference");
    }
    if (reference.contigs.empty()) {
        return makeError<Reference>(ErrorCode::kFormatError, "reference contains no contigs");
    }

    for (std::uint32_t i = 0; i < reference.contigs.size(); ++i) {
        const auto& contig = reference.contigs[i];
        if (contig.name.empty() || contig.sequence.empty() ||
            !reference.index.emplace(contig.name, i).second) {
            return makeError<Reference>(ErrorCode::kFormatError,
                                        "reference has an empty or duplicate contig");
        }
        reference.bases += contig.sequence.size();
    }
    reference.digest = digestReference(reference);
    return reference;
}

[[nodiscard]] auto parseU64(std::string_view text) -> std::optional<std::uint64_t> {
    std::uint64_t value = 0;
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto [ptr, error] = std::from_chars(begin, end, value);
    if (error != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] auto loadPaf(const std::filesystem::path& path)
    -> Result<std::unordered_map<std::string, PafAlignment>> {
    std::ifstream input(path);
    if (!input) {
        return makeError<std::unordered_map<std::string, PafAlignment>>(
            ErrorCode::kIOError, "failed to open PAF: " + path.string());
    }

    std::unordered_map<std::string, PafAlignment> alignments;
    std::string line;
    std::uint64_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (line.empty()) {
            continue;
        }
        std::vector<std::string_view> fields;
        std::string_view view(line);
        std::size_t start = 0;
        for (;;) {
            const auto tab = view.find('\t', start);
            fields.push_back(view.substr(start, tab == std::string_view::npos ? tab : tab - start));
            if (tab == std::string_view::npos) {
                break;
            }
            start = tab + 1;
        }
        if (fields.size() < 12) {
            return makeError<std::unordered_map<std::string, PafAlignment>>(
                ErrorCode::kFormatError,
                "PAF line " + std::to_string(lineNumber) + " has fewer than 12 fields");
        }

        const auto queryLength = parseU64(fields[1]);
        const auto queryStart = parseU64(fields[2]);
        const auto queryEnd = parseU64(fields[3]);
        const auto targetStart = parseU64(fields[7]);
        const auto targetEnd = parseU64(fields[8]);
        const auto mapq = parseU64(fields[11]);
        if (!queryLength || !queryStart || !queryEnd || !targetStart || !targetEnd || !mapq ||
            *queryEnd > *queryLength || *queryStart >= *queryEnd || *targetStart >= *targetEnd ||
            fields[4].size() != 1 || (fields[4][0] != '+' && fields[4][0] != '-')) {
            return makeError<std::unordered_map<std::string, PafAlignment>>(
                ErrorCode::kFormatError,
                "invalid PAF fields on line " + std::to_string(lineNumber));
        }

        PafAlignment alignment;
        alignment.target = std::string(fields[5]);
        alignment.queryLength = *queryLength;
        alignment.targetStart = *targetStart;
        alignment.targetEnd = *targetEnd;
        alignment.queryStart = *queryStart;
        alignment.queryEnd = *queryEnd;
        alignment.strand = fields[4][0];
        alignment.mapq = static_cast<std::uint32_t>(*mapq);
        bool primary = true;
        for (std::size_t i = 12; i < fields.size(); ++i) {
            if (fields[i].starts_with("NM:i:")) {
                const auto value = parseU64(fields[i].substr(5));
                if (!value || *value > std::numeric_limits<std::uint32_t>::max()) {
                    return makeError<std::unordered_map<std::string, PafAlignment>>(
                        ErrorCode::kFormatError,
                        "invalid NM tag on PAF line " + std::to_string(lineNumber));
                }
                alignment.mismatches = static_cast<std::uint32_t>(*value);
            } else if (fields[i].starts_with("cg:Z:")) {
                alignment.cigar = std::string(fields[i].substr(5));
            } else if (fields[i].starts_with("tp:A:")) {
                primary = fields[i].size() >= 6 && fields[i][5] == 'P';
            }
        }
        if (!primary) {
            continue;
        }

        const auto key = std::string(fields[0]);
        auto found = alignments.find(key);
        if (found == alignments.end() || alignment.mapq > found->second.mapq) {
            alignments[key] = std::move(alignment);
        }
    }
    if (!input.eof()) {
        return makeError<std::unordered_map<std::string, PafAlignment>>(ErrorCode::kIOError,
                                                                        "failed while reading PAF");
    }
    return alignments;
}

[[nodiscard]] auto parseCigar(std::string_view cigar) -> std::optional<std::vector<CigarOp>> {
    if (cigar.empty()) {
        return std::nullopt;
    }
    std::vector<CigarOp> operations;
    std::size_t offset = 0;
    while (offset < cigar.size()) {
        const auto digitStart = offset;
        while (offset < cigar.size() && cigar[offset] >= '0' && cigar[offset] <= '9') {
            ++offset;
        }
        if (offset == digitStart || offset == cigar.size()) {
            return std::nullopt;
        }
        const auto length = parseU64(cigar.substr(digitStart, offset - digitStart));
        if (!length || *length == 0 || *length > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        std::uint8_t code = 0;
        switch (cigar[offset++]) {
            case 'M':
            case '=':
            case 'X':
                code = 0;
                break;
            case 'I':
                code = 1;
                break;
            case 'D':
                code = 2;
                break;
            default:
                return std::nullopt;
        }
        if (!operations.empty() && operations.back().code == code) {
            if (operations.back().length > std::numeric_limits<std::uint32_t>::max() - *length) {
                return std::nullopt;
            }
            operations.back().length += static_cast<std::uint32_t>(*length);
        } else {
            operations.push_back({static_cast<std::uint32_t>(*length), code});
            if (operations.size() > kMaxCigarOps) {
                return std::nullopt;
            }
        }
    }
    return operations;
}

[[nodiscard]] auto isAcgtn(char base) -> bool {
    switch (base) {
        case 'A':
        case 'C':
        case 'G':
        case 'T':
        case 'N':
            return true;
        default:
            return false;
    }
}

[[nodiscard]] auto baseCode(char base) -> std::uint8_t {
    switch (base) {
        case 'A':
            return 0;
        case 'C':
            return 1;
        case 'G':
            return 2;
        case 'T':
            return 3;
        default:
            return 4;
    }
}

[[nodiscard]] auto codeBase(std::uint8_t code) -> char {
    switch (code) {
        case 0:
            return 'A';
        case 1:
            return 'C';
        case 2:
            return 'G';
        case 3:
            return 'T';
        default:
            return 'N';
    }
}

[[nodiscard]] auto complement(char base) -> char {
    switch (base) {
        case 'A':
            return 'T';
        case 'C':
            return 'G';
        case 'G':
            return 'C';
        case 'T':
            return 'A';
        default:
            return 'N';
    }
}

[[nodiscard]] auto reverseComplement(std::string_view sequence) -> std::string {
    std::string result;
    result.reserve(sequence.size());
    for (auto it = sequence.rbegin(); it != sequence.rend(); ++it) {
        result.push_back(complement(*it));
    }
    return result;
}

[[nodiscard]] auto commonPrefixLength(std::string_view left,
                                      std::string_view right) noexcept -> std::size_t {
    const auto limit = std::min(left.size(), right.size());
    std::size_t length = 0;
    while (length < limit && left[length] == right[length]) {
        ++length;
    }
    return length;
}

[[nodiscard]] auto buildIdStream(std::span<const ReadRecord> records) -> Bytes {
    Bytes output;
    appendVarint(output, records.size());
    for (const auto& record : records) {
        appendVarint(output, record.id.size());
        output.insert(output.end(), record.id.begin(), record.id.end());
        appendVarint(output, record.comment.size());
        output.insert(output.end(), record.comment.begin(), record.comment.end());
    }
    return output;
}

// 结构化测序 ID 在同一 run 内高度重复；front coding 先消掉与上一条记录的公共
// 前缀，再交给 zstd。随机 ID（例如 wgsim 合成数据）通常会让 zstd 直接赢，因此
// 编码端按块在 raw 与 front-coded 两种表示之间自适应选择。
[[nodiscard]] auto buildFrontCodedIdStream(std::span<const ReadRecord> records) -> Bytes {
    Bytes output;
    appendVarint(output, records.size());
    std::string_view previousId;
    std::string_view previousComment;
    for (const auto& record : records) {
        const auto idPrefix = commonPrefixLength(previousId, record.id);
        appendVarint(output, idPrefix);
        appendVarint(output, record.id.size() - idPrefix);
        output.insert(output.end(),
                      record.id.begin() + static_cast<std::ptrdiff_t>(idPrefix),
                      record.id.end());

        const auto commentPrefix = commonPrefixLength(previousComment, record.comment);
        appendVarint(output, commentPrefix);
        appendVarint(output, record.comment.size() - commentPrefix);
        output.insert(output.end(),
                      record.comment.begin() + static_cast<std::ptrdiff_t>(commentPrefix),
                      record.comment.end());

        previousId = record.id;
        previousComment = record.comment;
    }
    return output;
}

[[nodiscard]] auto decodeFrontCodedIdStream(std::span<const std::uint8_t> encoded,
                                            std::uint32_t recordCount) -> Result<Bytes> {
    Cursor cursor(encoded);
    auto count = cursor.readVarint();
    if (!count || *count != recordCount) {
        return makeError<Bytes>(ErrorCode::kFormatError, "front-coded ID count disagrees");
    }

    Bytes output;
    appendVarint(output, recordCount);
    std::string previousId;
    std::string previousComment;
    for (std::uint32_t index = 0; index < recordCount; ++index) {
        auto idPrefix = cursor.readVarint();
        auto idSuffixLength = cursor.readVarint();
        if (!idPrefix || !idSuffixLength || *idPrefix > previousId.size() ||
            *idSuffixLength > cursor.remaining()) {
            return makeError<Bytes>(ErrorCode::kFormatError, "invalid front-coded ID prefix");
        }
        auto idSuffix = cursor.readBytes(static_cast<std::size_t>(*idSuffixLength));
        if (!idSuffix) {
            return makeError<Bytes>(idSuffix.error());
        }
        std::string id = previousId.substr(0, static_cast<std::size_t>(*idPrefix));
        id.append(reinterpret_cast<const char*>(idSuffix->data()), idSuffix->size());

        auto commentPrefix = cursor.readVarint();
        auto commentSuffixLength = cursor.readVarint();
        if (!commentPrefix || !commentSuffixLength || *commentPrefix > previousComment.size() ||
            *commentSuffixLength > cursor.remaining()) {
            return makeError<Bytes>(ErrorCode::kFormatError, "invalid front-coded comment prefix");
        }
        auto commentSuffix = cursor.readBytes(static_cast<std::size_t>(*commentSuffixLength));
        if (!commentSuffix) {
            return makeError<Bytes>(commentSuffix.error());
        }
        std::string comment = previousComment.substr(0, static_cast<std::size_t>(*commentPrefix));
        comment.append(reinterpret_cast<const char*>(commentSuffix->data()), commentSuffix->size());

        appendVarint(output, id.size());
        output.insert(output.end(), id.begin(), id.end());
        appendVarint(output, comment.size());
        output.insert(output.end(), comment.begin(), comment.end());
        previousId = std::move(id);
        previousComment = std::move(comment);
    }
    if (cursor.remaining() != 0U) {
        return makeError<Bytes>(ErrorCode::kFormatError, "trailing bytes in front-coded ID stream");
    }
    return output;
}

[[nodiscard]] auto buildQualityStream(std::span<const ReadRecord> records) -> Bytes {
    Bytes output;
    appendVarint(output, records.size());
    for (const auto& record : records) {
        appendVarint(output, record.quality.size());
        output.insert(output.end(), record.quality.begin(), record.quality.end());
    }
    return output;
}

[[nodiscard]] auto encodeRead(const ReadRecord& record,
                              const Reference& reference,
                              const std::unordered_map<std::string, PafAlignment>& alignments,
                              std::uint32_t maxMismatches) -> EncodedRead {
    EncodedRead encoded;
    encoded.readLen = record.sequence.size();
    encoded.literal = record.sequence;

    const auto found = alignments.find(record.id);
    if (found == alignments.end()) {
        return encoded;
    }
    const auto& alignment = found->second;
    if (alignment.queryLength != record.sequence.size() ||
        alignment.queryStart >= alignment.queryEnd || alignment.queryEnd > alignment.queryLength ||
        alignment.targetStart >= alignment.targetEnd) {
        return encoded;
    }

    auto cigar = parseCigar(alignment.cigar);
    if (!cigar) {
        return encoded;
    }
    std::uint64_t querySpan = 0;
    std::uint64_t targetSpan = 0;
    for (const auto& op : *cigar) {
        if (op.code == 0 || op.code == 1) {
            querySpan += op.length;
        }
        if (op.code == 0 || op.code == 2) {
            targetSpan += op.length;
        }
    }
    if (querySpan != alignment.queryEnd - alignment.queryStart ||
        targetSpan != alignment.targetEnd - alignment.targetStart) {
        return encoded;
    }

    const auto contig = reference.index.find(alignment.target);
    if (contig == reference.index.end()) {
        return encoded;
    }
    const auto& sequence = reference.contigs[contig->second].sequence;
    if (alignment.targetEnd > sequence.size() ||
        !std::all_of(record.sequence.begin(), record.sequence.end(), isAcgtn)) {
        return encoded;
    }

    const auto originalMiddle =
        record.sequence.substr(static_cast<std::size_t>(alignment.queryStart),
                               static_cast<std::size_t>(alignment.queryEnd - alignment.queryStart));
    const auto orientedMiddle =
        alignment.strand == '-' ? reverseComplement(originalMiddle) : originalMiddle;
    const auto referenceSlice = std::string_view(sequence).substr(
        static_cast<std::size_t>(alignment.targetStart), static_cast<std::size_t>(targetSpan));

    std::uint64_t queryOffset = 0;
    std::uint64_t targetOffset = 0;
    std::uint32_t mismatches = 0;
    std::uint32_t previous = 0;
    std::vector<std::uint8_t> insertionBases;
    std::vector<std::pair<std::uint32_t, std::uint8_t>> edits;
    for (const auto& op : *cigar) {
        switch (op.code) {
            case 0:
                for (std::uint32_t i = 0; i < op.length; ++i) {
                    const auto queryPosition = queryOffset + i;
                    if (referenceSlice[targetOffset + i] != orientedMiddle[queryPosition]) {
                        edits.emplace_back(static_cast<std::uint32_t>(queryPosition) - previous,
                                           baseCode(orientedMiddle[queryPosition]));
                        previous = static_cast<std::uint32_t>(queryPosition);
                        ++mismatches;
                    }
                }
                queryOffset += op.length;
                targetOffset += op.length;
                break;
            case 1:
                for (std::uint32_t i = 0; i < op.length; ++i) {
                    insertionBases.push_back(baseCode(orientedMiddle[queryOffset + i]));
                }
                queryOffset += op.length;
                break;
            case 2:
                targetOffset += op.length;
                break;
            default:
                return encoded;
        }
    }
    if (mismatches > maxMismatches) {
        return encoded;
    }

    encoded.flags = kReadFlagMapped;
    if (alignment.strand == '-') {
        encoded.flags |= kReadFlagReverse;
    }
    encoded.contigId = contig->second;
    encoded.targetStart = alignment.targetStart;
    encoded.queryStart = alignment.queryStart;
    encoded.queryEnd = alignment.queryEnd;
    encoded.mismatches = mismatches;
    encoded.edits = std::move(edits);
    encoded.literal.clear();

    const bool simple = alignment.queryStart == 0 && alignment.queryEnd == record.sequence.size() &&
        querySpan == record.sequence.size() && targetSpan == record.sequence.size() &&
        cigar->size() == 1 && (*cigar)[0].code == 0;
    if (!simple) {
        encoded.flags |= kReadFlagExtended;
        encoded.cigar = std::move(*cigar);
        encoded.insertionBases = std::move(insertionBases);
        encoded.leftClip =
            record.sequence.substr(0, static_cast<std::size_t>(alignment.queryStart));
        encoded.rightClip = record.sequence.substr(static_cast<std::size_t>(alignment.queryEnd));
    }
    return encoded;
}

[[nodiscard]] auto encodeBlock(std::span<const ReadRecord> records,
                               const Reference& reference,
                               const std::unordered_map<std::string, PafAlignment>& alignments,
                               std::uint32_t maxMismatches) -> std::pair<Bytes, BlockStats> {
    Bytes alignmentsRaw;
    BlockStats stats;
    for (const auto& record : records) {
        const auto encoded = encodeRead(record, reference, alignments, maxMismatches);
        appendU8(alignmentsRaw, encoded.flags);
        appendVarint(alignmentsRaw, encoded.readLen);
        if ((encoded.flags & kReadFlagMapped) != 0U) {
            ++stats.mapped;
            appendVarint(alignmentsRaw, encoded.contigId);
            appendVarint(alignmentsRaw, encoded.targetStart);
            if ((encoded.flags & kReadFlagExtended) != 0U) {
                if (encoded.queryStart != 0 || encoded.queryEnd != encoded.readLen) {
                    ++stats.clipped;
                }
                bool hasIndel = false;
                for (const auto& op : encoded.cigar) {
                    if (op.code != 0) {
                        hasIndel = true;
                        break;
                    }
                }
                if (hasIndel) {
                    ++stats.indel;
                }
                appendVarint(alignmentsRaw, encoded.queryStart);
                appendVarint(alignmentsRaw, encoded.queryEnd);
                appendVarint(alignmentsRaw, encoded.cigar.size());
                std::size_t insertionOffset = 0;
                for (const auto& op : encoded.cigar) {
                    appendVarint(alignmentsRaw, op.length);
                    appendU8(alignmentsRaw, op.code);
                    if (op.code == 1) {
                        for (std::uint32_t i = 0; i < op.length; ++i) {
                            appendU8(alignmentsRaw, encoded.insertionBases[insertionOffset + i]);
                        }
                        insertionOffset += op.length;
                    }
                }
                appendVarint(alignmentsRaw, encoded.leftClip.size());
                for (const auto base : encoded.leftClip) {
                    appendU8(alignmentsRaw, baseCode(base));
                }
                appendVarint(alignmentsRaw, encoded.rightClip.size());
                for (const auto base : encoded.rightClip) {
                    appendU8(alignmentsRaw, baseCode(base));
                }
            }
            appendVarint(alignmentsRaw, encoded.mismatches);
            for (const auto& [delta, base] : encoded.edits) {
                appendVarint(alignmentsRaw, delta);
                appendU8(alignmentsRaw, base);
            }
        } else {
            ++stats.literal;
            alignmentsRaw.insert(
                alignmentsRaw.end(), encoded.literal.begin(), encoded.literal.end());
        }
    }
    return {std::move(alignmentsRaw), stats};
}

[[nodiscard]] auto blockChecksum(std::span<const std::uint8_t> ids,
                                 std::span<const std::uint8_t> qualities,
                                 std::span<const std::uint8_t> alignments) -> std::uint64_t {
    auto* state = XXH64_createState();
    XXH64_reset(state, 0);
    XXH64_update(state, ids.data(), ids.size());
    XXH64_update(state, qualities.data(), qualities.size());
    XXH64_update(state, alignments.data(), alignments.size());
    const auto digest = XXH64_digest(state);
    XXH64_freeState(state);
    return digest;
}

[[nodiscard]] auto writeGlobalHeader(std::ostream& output,
                                     const Reference& reference) -> VoidResult {
    Bytes table;
    for (const auto& contig : reference.contigs) {
        appendVarint(table, contig.name.size());
        table.insert(table.end(), contig.name.begin(), contig.name.end());
        appendVarint(table, contig.sequence.size());
    }
    if (table.size() > kMaxHeaderTableBytes ||
        reference.contigs.size() > std::numeric_limits<std::uint32_t>::max() ||
        kGlobalHeaderSize + table.size() > std::numeric_limits<std::uint16_t>::max()) {
        return makeVoidError(ErrorCode::kUsageError, "reference contig table is too large");
    }

    Bytes header;
    header.reserve(kGlobalHeaderSize + table.size());
    header.insert(header.end(), kMagic.begin(), kMagic.end());
    appendU16(header, kVersion);
    appendU16(header, static_cast<std::uint16_t>(kGlobalHeaderSize + table.size()));
    appendU32(header, 0);
    appendU32(header, static_cast<std::uint32_t>(reference.contigs.size()));
    appendU64(header, reference.bases);
    appendU64(header, reference.digest);
    appendU64(header, table.size());
    appendU64(header, 0);
    header.insert(header.end(), table.begin(), table.end());

    const auto checksum = XXH64(header.data(), header.size(), 0);
    for (unsigned i = 0; i < 8; ++i) {
        header[44 + i] = static_cast<std::uint8_t>(checksum >> (i * 8U));
    }
    return writeBytes(output, header);
}

[[nodiscard]] auto readGlobalHeader(std::istream& input) -> Result<GlobalHeader> {
    std::array<std::uint8_t, kGlobalHeaderSize> fixed{};
    FQC_TRY(readExact(input, fixed));
    Cursor cursor(fixed);
    auto magic = cursor.readBytes(kMagic.size());
    auto version = cursor.readU16();
    auto headerSize = cursor.readU16();
    auto flags = cursor.readU32();
    auto contigCount = cursor.readU32();
    auto referenceBases = cursor.readU64();
    auto referenceDigest = cursor.readU64();
    auto tableBytes = cursor.readU64();
    auto storedChecksum = cursor.readU64();
    if (!magic || !version || !headerSize || !flags || !contigCount || !referenceBases ||
        !referenceDigest || !tableBytes || !storedChecksum ||
        !std::equal(kMagic.begin(), kMagic.end(), magic->begin()) || *version != kVersion ||
        *flags != 0U || *headerSize != kGlobalHeaderSize + *tableBytes ||
        *tableBytes > kMaxHeaderTableBytes || *contigCount == 0U ||
        *contigCount > std::numeric_limits<std::uint32_t>::max()) {
        return makeError<GlobalHeader>(ErrorCode::kFormatError,
                                       "invalid fqc-max-ref/v1 global header");
    }

    Bytes table(static_cast<std::size_t>(*tableBytes));
    FQC_TRY(readExact(input, table));
    Bytes full;
    full.reserve(fixed.size() + table.size());
    full.insert(full.end(), fixed.begin(), fixed.end());
    full.insert(full.end(), table.begin(), table.end());
    auto checksumHeader = full;
    std::fill(checksumHeader.begin() + 44, checksumHeader.begin() + 52, 0);
    const auto checksum = XXH64(checksumHeader.data(), checksumHeader.size(), 0);
    if (checksum != *storedChecksum) {
        return makeError<GlobalHeader>(ErrorCode::kChecksumError,
                                       "global header checksum mismatch");
    }

    Cursor tableCursor(table);
    GlobalHeader header;
    header.contigs.reserve(*contigCount);
    std::uint64_t totalBases = 0;
    for (std::uint32_t i = 0; i < *contigCount; ++i) {
        auto nameLength = tableCursor.readVarint();
        if (!nameLength || *nameLength == 0 || *nameLength > tableCursor.remaining()) {
            return makeError<GlobalHeader>(ErrorCode::kFormatError,
                                           "invalid reference contig name");
        }
        auto nameBytes = tableCursor.readBytes(static_cast<std::size_t>(*nameLength));
        auto length = tableCursor.readVarint();
        if (!nameBytes || !length || *length == 0 || !checkedAdd(totalBases, *length)) {
            return makeError<GlobalHeader>(ErrorCode::kFormatError,
                                           "invalid reference contig length");
        }
        header.contigs.emplace_back(
            std::string(reinterpret_cast<const char*>(nameBytes->data()), nameBytes->size()),
            *length);
    }
    if (tableCursor.remaining() != 0U || totalBases != *referenceBases) {
        return makeError<GlobalHeader>(ErrorCode::kFormatError,
                                       "reference contig table totals disagree");
    }
    header.referenceBases = *referenceBases;
    header.digest = *referenceDigest;
    return header;
}

[[nodiscard]] auto writeBlock(std::ostream& output,
                              std::uint32_t blockId,
                              std::span<const ReadRecord> records,
                              const Reference& reference,
                              const std::unordered_map<std::string, PafAlignment>& alignments,
                              std::uint32_t maxMismatches)
    -> Result<std::pair<std::uint64_t, BlockStats>> {
    const auto idsRaw = buildIdStream(records);
    const auto idsFront = buildFrontCodedIdStream(records);
    const auto qualitiesRaw = buildQualityStream(records);
    auto [alignmentsRaw, blockStats] = encodeBlock(records, reference, alignments, maxMismatches);

    FQC_TRY_ASSIGN(rawIds, zstdCompress(idsRaw, kZstdLevel));
    FQC_TRY_ASSIGN(frontIds, zstdCompress(idsFront, kZstdLevel));
    FQC_TRY_ASSIGN(zstdQualities, zstdCompress(qualitiesRaw, kZstdLevel));
    FQC_TRY_ASSIGN(alignmentsCompressed, zstdCompress(alignmentsRaw, kZstdLevel));

    if (idsFront.size() > std::numeric_limits<std::uint32_t>::max()) {
        return makeError<std::pair<std::uint64_t, BlockStats>>(
            ErrorCode::kUsageError, "front-coded ID stream exceeds 4 GiB");
    }
    std::uint8_t idCodec = kIdCodecRawZstd;
    Bytes ids = std::move(rawIds);
    std::uint32_t codecIdsSize = 0;
    if (frontIds.size() < ids.size()) {
        idCodec = kIdCodecFrontZstd;
        codecIdsSize = static_cast<std::uint32_t>(idsFront.size());
        ids = std::move(frontIds);
        ++blockStats.frontIdBlocks;
    }

    std::uint8_t qualityCodec = 0;
    Bytes qualities = std::move(zstdQualities);
    auto fqz = format::compressFqzQuality(records);
    if (fqz && fqz->bytes.size() < qualities.size()) {
        qualityCodec = 1;
        qualities = std::move(fqz->bytes);
    }

    blockStats.idsBytes = ids.size();
    blockStats.qualitiesBytes = qualities.size();
    blockStats.alignmentsBytes = alignmentsCompressed.size();
    if (qualityCodec == 1) {
        ++blockStats.fqzBlocks;
    } else {
        ++blockStats.zstdQualityBlocks;
    }

    const auto checksum = blockChecksum(idsRaw, qualitiesRaw, alignmentsRaw);
    Bytes header;
    header.reserve(kBlockHeaderSize);
    appendU32(header, kBlockMagic);
    appendU32(header, static_cast<std::uint32_t>(kBlockHeaderSize));
    appendU32(header, blockId);
    appendU32(header, static_cast<std::uint32_t>(records.size()));
    appendU64(header, idsRaw.size());
    appendU64(header, ids.size());
    appendU64(header, qualitiesRaw.size());
    appendU64(header, qualities.size());
    appendU64(header, alignmentsRaw.size());
    appendU64(header, alignmentsCompressed.size());
    appendU64(header, checksum);
    appendU8(header, idCodec);
    appendU8(header, qualityCodec);
    appendU16(header, 0);
    appendU64(header, 0);
    appendU32(header, codecIdsSize);
    const auto headerChecksum = XXH64(header.data(), header.size(), 0);
    for (unsigned i = 0; i < 8; ++i) {
        header[76 + i] = static_cast<std::uint8_t>(headerChecksum >> (i * 8U));
    }

    FQC_TRY(writeBytes(output, header));
    FQC_TRY(writeBytes(output, ids));
    FQC_TRY(writeBytes(output, qualities));
    FQC_TRY(writeBytes(output, alignmentsCompressed));
    return std::pair(checksum, blockStats);
}

[[nodiscard]] auto decodeBlock(std::span<const std::uint8_t> idsRaw,
                               std::span<const std::uint8_t> qualitiesRaw,
                               std::span<const std::uint8_t> alignments,
                               std::uint32_t recordCount,
                               const Reference& reference) -> Result<DecodedBlock> {
    Cursor idCursor(idsRaw);
    auto idCount = idCursor.readVarint();
    if (!idCount || *idCount != recordCount) {
        return makeError<DecodedBlock>(ErrorCode::kFormatError, "ID stream count disagrees");
    }
    std::vector<std::string> ids;
    std::vector<std::string> comments;
    ids.reserve(recordCount);
    comments.reserve(recordCount);
    for (std::uint32_t i = 0; i < recordCount; ++i) {
        auto idLength = idCursor.readVarint();
        if (!idLength) {
            return makeError<DecodedBlock>(ErrorCode::kFormatError, "truncated ID stream");
        }
        auto idBytes = idCursor.readBytes(static_cast<std::size_t>(*idLength));
        auto commentLength = idCursor.readVarint();
        if (!idBytes || !commentLength) {
            return makeError<DecodedBlock>(ErrorCode::kFormatError, "truncated ID stream");
        }
        auto commentBytes = idCursor.readBytes(static_cast<std::size_t>(*commentLength));
        if (!commentBytes) {
            return makeError<DecodedBlock>(ErrorCode::kFormatError, "truncated ID stream");
        }
        ids.emplace_back(reinterpret_cast<const char*>(idBytes->data()), idBytes->size());
        comments.emplace_back(reinterpret_cast<const char*>(commentBytes->data()),
                              commentBytes->size());
    }
    if (idCursor.remaining() != 0U) {
        return makeError<DecodedBlock>(ErrorCode::kFormatError, "trailing bytes in ID stream");
    }

    Cursor qualityCursor(qualitiesRaw);
    auto qualityCount = qualityCursor.readVarint();
    if (!qualityCount || *qualityCount != recordCount) {
        return makeError<DecodedBlock>(ErrorCode::kFormatError, "quality stream count disagrees");
    }
    std::vector<std::string> qualities;
    qualities.reserve(recordCount);
    for (std::uint32_t i = 0; i < recordCount; ++i) {
        auto length = qualityCursor.readVarint();
        if (!length) {
            return makeError<DecodedBlock>(ErrorCode::kFormatError, "truncated quality stream");
        }
        auto bytes = qualityCursor.readBytes(static_cast<std::size_t>(*length));
        if (!bytes) {
            return makeError<DecodedBlock>(ErrorCode::kFormatError, "truncated quality stream");
        }
        qualities.emplace_back(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    }
    if (qualityCursor.remaining() != 0U) {
        return makeError<DecodedBlock>(ErrorCode::kFormatError, "trailing bytes in quality stream");
    }

    DecodedBlock block;
    block.records.reserve(recordCount);
    Cursor alignmentCursor(alignments);
    for (std::uint32_t i = 0; i < recordCount; ++i) {
        auto flags = alignmentCursor.readU8();
        auto readLength = alignmentCursor.readVarint();
        if (!flags || !readLength || *readLength == 0 || *readLength != qualities[i].size() ||
            (*flags & ~0x0FU) != 0U) {
            return makeError<DecodedBlock>(ErrorCode::kFormatError, "invalid read flags/length");
        }
        const bool literal = (*flags & kReadFlagLiteral) != 0U;
        const bool mapped = (*flags & kReadFlagMapped) != 0U;
        const bool reverse = (*flags & kReadFlagReverse) != 0U;
        const bool extended = (*flags & kReadFlagExtended) != 0U;
        if (literal == mapped || (literal && (reverse || extended))) {
            return makeError<DecodedBlock>(ErrorCode::kFormatError,
                                           "read must be mapped xor literal");
        }

        const auto applyMismatches =
            [&](std::string& target, std::uint64_t count, std::uint64_t limit) -> bool {
            std::uint64_t previous = 0;
            for (std::uint64_t edit = 0; edit < count; ++edit) {
                auto delta = alignmentCursor.readVarint();
                auto base = alignmentCursor.readU8();
                if (!delta || !base || *base > 4U) {
                    return false;
                }
                const auto position = previous + *delta;
                if (position >= limit || (edit > 0 && position <= previous)) {
                    return false;
                }
                target[static_cast<std::size_t>(position)] = codeBase(*base);
                previous = position;
            }
            return true;
        };

        std::string sequence;
        if (literal) {
            auto bytes = alignmentCursor.readBytes(static_cast<std::size_t>(*readLength));
            if (!bytes) {
                return makeError<DecodedBlock>(ErrorCode::kFormatError, "truncated literal");
            }
            sequence.assign(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            ++block.stats.literal;
        } else {
            auto contigId = alignmentCursor.readVarint();
            auto targetStart = alignmentCursor.readVarint();
            if (!contigId || !targetStart || *contigId >= reference.contigs.size() ||
                *targetStart > reference.contigs[*contigId].sequence.size()) {
                return makeError<DecodedBlock>(ErrorCode::kFormatError,
                                               "invalid mapped read coordinates");
            }
            const auto& referenceSequence = reference.contigs[*contigId].sequence;

            if (extended) {
                auto queryStart = alignmentCursor.readVarint();
                auto queryEnd = alignmentCursor.readVarint();
                auto operationCount = alignmentCursor.readVarint();
                if (!queryStart || !queryEnd || !operationCount || *queryStart >= *queryEnd ||
                    *queryEnd > *readLength || *operationCount == 0 ||
                    *operationCount > kMaxCigarOps) {
                    return makeError<DecodedBlock>(ErrorCode::kFormatError,
                                                   "invalid extended mapped coordinates");
                }

                std::vector<CigarOp> operations;
                std::vector<std::uint8_t> insertionBases;
                std::uint64_t querySpan = 0;
                std::uint64_t targetSpan = 0;
                for (std::uint64_t operation = 0; operation < *operationCount; ++operation) {
                    auto length = alignmentCursor.readVarint();
                    auto code = alignmentCursor.readU8();
                    if (!length || !code || *length == 0 ||
                        *length > std::numeric_limits<std::uint32_t>::max() || *code > 2U) {
                        return makeError<DecodedBlock>(ErrorCode::kFormatError,
                                                       "invalid CIGAR operation");
                    }
                    operations.push_back(
                        {static_cast<std::uint32_t>(*length), static_cast<std::uint8_t>(*code)});
                    if (*code == 1U) {
                        auto inserted =
                            alignmentCursor.readBytes(static_cast<std::size_t>(*length));
                        if (!inserted) {
                            return makeError<DecodedBlock>(ErrorCode::kFormatError,
                                                           "truncated insertion bases");
                        }
                        for (const auto base : *inserted) {
                            if (base > 4U) {
                                return makeError<DecodedBlock>(ErrorCode::kFormatError,
                                                               "invalid insertion base");
                            }
                            insertionBases.push_back(base);
                        }
                    }
                    if ((*code == 0U || *code == 1U) &&
                        !checkedAdd(querySpan, static_cast<std::size_t>(*length))) {
                        return makeError<DecodedBlock>(ErrorCode::kFormatError,
                                                       "query span overflow");
                    }
                    if ((*code == 0U || *code == 2U) &&
                        !checkedAdd(targetSpan, static_cast<std::size_t>(*length))) {
                        return makeError<DecodedBlock>(ErrorCode::kFormatError,
                                                       "target span overflow");
                    }
                }
                if (querySpan != *queryEnd - *queryStart ||
                    *targetStart > referenceSequence.size() ||
                    targetSpan > referenceSequence.size() - *targetStart) {
                    return makeError<DecodedBlock>(ErrorCode::kFormatError,
                                                   "CIGAR spans disagree with coordinates");
                }

                auto leftClipLength = alignmentCursor.readVarint();
                if (!leftClipLength || *leftClipLength != *queryStart ||
                    *leftClipLength > alignmentCursor.remaining()) {
                    return makeError<DecodedBlock>(ErrorCode::kFormatError,
                                                   "invalid left clip length");
                }
                std::string leftClip;
                leftClip.reserve(static_cast<std::size_t>(*leftClipLength));
                for (std::uint64_t base = 0; base < *leftClipLength; ++base) {
                    auto code = alignmentCursor.readU8();
                    if (!code || *code > 4U) {
                        return makeError<DecodedBlock>(ErrorCode::kFormatError,
                                                       "invalid left clip base");
                    }
                    leftClip.push_back(codeBase(*code));
                }
                auto rightClipLength = alignmentCursor.readVarint();
                if (!rightClipLength || *rightClipLength != *readLength - *queryEnd ||
                    *rightClipLength > alignmentCursor.remaining()) {
                    return makeError<DecodedBlock>(ErrorCode::kFormatError,
                                                   "invalid right clip length");
                }
                std::string rightClip;
                rightClip.reserve(static_cast<std::size_t>(*rightClipLength));
                for (std::uint64_t base = 0; base < *rightClipLength; ++base) {
                    auto code = alignmentCursor.readU8();
                    if (!code || *code > 4U) {
                        return makeError<DecodedBlock>(ErrorCode::kFormatError,
                                                       "invalid right clip base");
                    }
                    rightClip.push_back(codeBase(*code));
                }

                std::string aligned;
                aligned.reserve(static_cast<std::size_t>(querySpan));
                std::size_t insertionOffset = 0;
                std::uint64_t targetOffset = *targetStart;
                for (const auto& operation : operations) {
                    if (operation.code == 0U) {
                        if (operation.length > referenceSequence.size() - targetOffset) {
                            return makeError<DecodedBlock>(ErrorCode::kFormatError,
                                                           "CIGAR target exceeds reference");
                        }
                        aligned.append(referenceSequence,
                                       static_cast<std::size_t>(targetOffset),
                                       operation.length);
                        targetOffset += operation.length;
                    } else if (operation.code == 1U) {
                        if (insertionOffset + operation.length > insertionBases.size()) {
                            return makeError<DecodedBlock>(ErrorCode::kFormatError,
                                                           "insertion length disagrees");
                        }
                        for (std::uint32_t baseIndex = 0; baseIndex < operation.length;
                             ++baseIndex) {
                            aligned.push_back(
                                codeBase(insertionBases[insertionOffset + baseIndex]));
                        }
                        insertionOffset += operation.length;
                    } else if (operation.length > referenceSequence.size() - targetOffset) {
                        return makeError<DecodedBlock>(ErrorCode::kFormatError,
                                                       "CIGAR target exceeds reference");
                    } else {
                        targetOffset += operation.length;
                    }
                }
                if (insertionOffset != insertionBases.size()) {
                    return makeError<DecodedBlock>(ErrorCode::kFormatError,
                                                   "trailing insertion bases");
                }

                auto mismatchCount = alignmentCursor.readVarint();
                if (!mismatchCount || *mismatchCount > querySpan ||
                    !applyMismatches(aligned, *mismatchCount, querySpan)) {
                    return makeError<DecodedBlock>(ErrorCode::kFormatError,
                                                   "invalid extended mismatch entry");
                }
                if (reverse) {
                    aligned = reverseComplement(aligned);
                }
                sequence = std::move(leftClip);
                sequence += aligned;
                sequence += rightClip;
                if (sequence.size() != *readLength) {
                    return makeError<DecodedBlock>(ErrorCode::kFormatError,
                                                   "extended read length disagrees");
                }
                if (*queryStart != 0U || *queryEnd != *readLength) {
                    ++block.stats.clipped;
                }
                if (std::any_of(operations.begin(), operations.end(), [](const CigarOp& operation) {
                        return operation.code != 0U;
                    })) {
                    ++block.stats.indel;
                }
            } else {
                auto mismatchCount = alignmentCursor.readVarint();
                if (!mismatchCount || *readLength > referenceSequence.size() - *targetStart ||
                    *mismatchCount > *readLength) {
                    return makeError<DecodedBlock>(ErrorCode::kFormatError,
                                                   "invalid mapped read coordinates");
                }
                sequence = referenceSequence.substr(static_cast<std::size_t>(*targetStart),
                                                    static_cast<std::size_t>(*readLength));
                if (!applyMismatches(sequence, *mismatchCount, *readLength)) {
                    return makeError<DecodedBlock>(ErrorCode::kFormatError,
                                                   "invalid mismatch entry");
                }
                if (reverse) {
                    sequence = reverseComplement(sequence);
                }
            }
            ++block.stats.mapped;
        }

        if (sequence.size() != qualities[i].size()) {
            return makeError<DecodedBlock>(ErrorCode::kFormatError,
                                           "sequence/quality length mismatch");
        }
        block.records.push_back({std::move(ids[i]),
                                 std::move(comments[i]),
                                 std::move(sequence),
                                 std::move(qualities[i])});
    }
    if (alignmentCursor.remaining() != 0U) {
        return makeError<DecodedBlock>(ErrorCode::kFormatError,
                                       "trailing bytes in alignment stream");
    }
    return block;
}

[[nodiscard]] auto readBlock(std::istream& input,
                             std::uint32_t expectedBlockId,
                             const Reference& reference,
                             RefStats& stats,
                             std::uint64_t& globalChecksum)
    -> Result<std::optional<std::vector<ReadRecord>>> {
    std::array<std::uint8_t, 4> markerBytes{};
    FQC_TRY(readExact(input, markerBytes));
    Cursor markerCursor(markerBytes);
    auto marker = markerCursor.readU32();
    if (!marker) {
        return makeError<std::optional<std::vector<ReadRecord>>>(marker.error());
    }
    if (*marker == kFooterMagic) {
        std::array<std::uint8_t, kFooterSize> footer{};
        std::copy(markerBytes.begin(), markerBytes.end(), footer.begin());
        FQC_TRY(readExact(input, std::span(footer).subspan(4)));
        Cursor cursor(footer);
        auto footerMagic = cursor.readU32();
        auto size = cursor.readU32();
        auto blockCount = cursor.readU64();
        auto recordCount = cursor.readU64();
        auto totalBases = cursor.readU64();
        auto checksum = cursor.readU64();
        if (!footerMagic || !size || !blockCount || !recordCount || !totalBases || !checksum ||
            *size != kFooterSize || *blockCount != stats.blocks || *recordCount != stats.records ||
            *totalBases != stats.bases || *checksum != globalChecksum) {
            return makeError<std::optional<std::vector<ReadRecord>>>(
                ErrorCode::kFormatError, "invalid fqc-max-ref/v1 footer");
        }
        if (input.peek() != std::char_traits<char>::eof()) {
            return makeError<std::optional<std::vector<ReadRecord>>>(ErrorCode::kFormatError,
                                                                     "trailing bytes after footer");
        }
        if (input.bad()) {
            return makeError<std::optional<std::vector<ReadRecord>>>(
                ErrorCode::kIOError, "failed while checking footer");
        }
        return std::optional<std::vector<ReadRecord>>{};
    }
    if (*marker != kBlockMagic) {
        return makeError<std::optional<std::vector<ReadRecord>>>(ErrorCode::kFormatError,
                                                                 "invalid block marker");
    }

    std::array<std::uint8_t, kBlockHeaderSize> header{};
    std::copy(markerBytes.begin(), markerBytes.end(), header.begin());
    FQC_TRY(readExact(input, std::span(header).subspan(4)));
    Cursor cursor(header);
    auto blockMagic = cursor.readU32();
    auto headerSize = cursor.readU32();
    auto blockId = cursor.readU32();
    auto recordCount = cursor.readU32();
    auto rawIdsSize = cursor.readU64();
    auto idsSize = cursor.readU64();
    auto rawQualitiesSize = cursor.readU64();
    auto qualitiesSize = cursor.readU64();
    auto rawAlignmentsSize = cursor.readU64();
    auto alignmentsSize = cursor.readU64();
    auto checksum = cursor.readU64();
    auto idCodec = cursor.readU8();
    auto qualityCodec = cursor.readU8();
    auto reserved = cursor.readU16();
    auto headerChecksum = cursor.readU64();
    auto codecIdsSize = cursor.readU32();
    if (!blockMagic || !headerSize || !blockId || !recordCount || !rawIdsSize || !idsSize ||
        !rawQualitiesSize || !qualitiesSize || !rawAlignmentsSize || !alignmentsSize || !checksum ||
        !idCodec || !qualityCodec || !reserved || !headerChecksum || !codecIdsSize ||
        *headerSize != kBlockHeaderSize || *blockId != expectedBlockId || *recordCount == 0 ||
        (*idCodec != kIdCodecRawZstd && *idCodec != kIdCodecFrontZstd) || *qualityCodec > 1 ||
        *reserved != 0U || (*idCodec == kIdCodecRawZstd && *codecIdsSize != 0U) ||
        (*idCodec == kIdCodecFrontZstd && *codecIdsSize == 0U)) {
        return makeError<std::optional<std::vector<ReadRecord>>>(ErrorCode::kFormatError,
                                                                 "invalid block header");
    }
    auto headerCopy = header;
    std::fill(headerCopy.begin() + 76, headerCopy.begin() + 84, 0);
    if (XXH64(headerCopy.data(), headerCopy.size(), 0) != *headerChecksum) {
        return makeError<std::optional<std::vector<ReadRecord>>>(ErrorCode::kFormatError,
                                                                 "block header checksum mismatch");
    }
    for (const auto size : {*rawIdsSize,
                            *idsSize,
                            *rawQualitiesSize,
                            *qualitiesSize,
                            *rawAlignmentsSize,
                            *alignmentsSize,
                            static_cast<std::uint64_t>(*codecIdsSize)}) {
        if (size > kMaxStreamBytes || size > std::numeric_limits<std::size_t>::max()) {
            return makeError<std::optional<std::vector<ReadRecord>>>(
                ErrorCode::kFormatError, "block stream exceeds size limit");
        }
    }
    if (*recordCount > kMaxStreamBytes) {
        return makeError<std::optional<std::vector<ReadRecord>>>(
            ErrorCode::kFormatError, "block record count exceeds limit");
    }

    Bytes idsCompressed(static_cast<std::size_t>(*idsSize));
    Bytes qualitiesCompressed(static_cast<std::size_t>(*qualitiesSize));
    Bytes alignments(static_cast<std::size_t>(*alignmentsSize));
    FQC_TRY(readExact(input, idsCompressed));
    FQC_TRY(readExact(input, qualitiesCompressed));
    FQC_TRY(readExact(input, alignments));

    const auto idsPayloadSize = *idCodec == kIdCodecFrontZstd
        ? static_cast<std::size_t>(*codecIdsSize)
        : static_cast<std::size_t>(*rawIdsSize);
    FQC_TRY_ASSIGN(idsPayload, zstdDecompress(idsCompressed, idsPayloadSize));
    Bytes ids;
    if (*idCodec == kIdCodecFrontZstd) {
        FQC_TRY_ASSIGN(decodedIds, decodeFrontCodedIdStream(idsPayload, *recordCount));
        if (decodedIds.size() != *rawIdsSize) {
            return makeError<std::optional<std::vector<ReadRecord>>>(
                ErrorCode::kFormatError, "front-coded ID stream size disagrees");
        }
        ids = std::move(decodedIds);
    } else {
        ids = std::move(idsPayload);
    }
    FQC_TRY_ASSIGN(alignmentsRaw,
                   zstdDecompress(alignments, static_cast<std::size_t>(*rawAlignmentsSize)));
    auto qualities = *qualityCodec == 1
        ? format::decompressFqzQuality(
              qualitiesCompressed, *recordCount, static_cast<std::size_t>(*rawQualitiesSize))
        : zstdDecompress(qualitiesCompressed, static_cast<std::size_t>(*rawQualitiesSize));
    if (!qualities) {
        return makeError<std::optional<std::vector<ReadRecord>>>(qualities.error());
    }
    const auto actualChecksum = blockChecksum(ids, *qualities, alignmentsRaw);
    if (actualChecksum != *checksum) {
        return makeError<std::optional<std::vector<ReadRecord>>>(ErrorCode::kChecksumError,
                                                                 "block checksum mismatch");
    }

    auto block = decodeBlock(ids, *qualities, alignmentsRaw, *recordCount, reference);
    if (!block) {
        return makeError<std::optional<std::vector<ReadRecord>>>(block.error());
    }
    block->stats.idsBytes = *idsSize;
    block->stats.qualitiesBytes = *qualitiesSize;
    block->stats.alignmentsBytes = *alignmentsSize;
    if (*qualityCodec == 1) {
        ++block->stats.fqzBlocks;
    } else {
        ++block->stats.zstdQualityBlocks;
    }
    if (*idCodec == kIdCodecFrontZstd) {
        ++block->stats.frontIdBlocks;
    }
    stats.blocks += 1;
    stats.records += block->records.size();
    stats.mapped += block->stats.mapped;
    stats.literal += block->stats.literal;
    stats.clipped += block->stats.clipped;
    stats.indel += block->stats.indel;
    stats.idsBytes += block->stats.idsBytes;
    stats.qualitiesBytes += block->stats.qualitiesBytes;
    stats.alignmentsBytes += block->stats.alignmentsBytes;
    stats.fqzBlocks += block->stats.fqzBlocks;
    stats.zstdQualityBlocks += block->stats.zstdQualityBlocks;
    stats.frontIdBlocks += block->stats.frontIdBlocks;
    for (const auto& record : block->records) {
        stats.bases += record.sequence.size();
    }
    globalChecksum = format::advanceGlobalChecksum(globalChecksum, *checksum);
    return std::optional<std::vector<ReadRecord>>(std::move(block->records));
}

void writeFastqRecord(std::ostream& output, const ReadRecord& record) {
    output << '@' << record.id;
    if (!record.comment.empty()) {
        output << record.comment;
    }
    output << '\n' << record.sequence << "\n+\n" << record.quality << '\n';
}

[[nodiscard]] auto decodeArchive(const std::filesystem::path& archivePath,
                                 const std::filesystem::path& referencePath,
                                 std::ostream* output) -> Result<RefStats> {
    FQC_TRY_ASSIGN(reference, loadReference(referencePath));
    std::ifstream input(archivePath, std::ios::binary);
    if (!input) {
        return makeError<RefStats>(ErrorCode::kIOError,
                                   "failed to open archive: " + archivePath.string());
    }

    FQC_TRY_ASSIGN(header, readGlobalHeader(input));
    if (header.contigs.size() != reference.contigs.size() ||
        header.referenceBases != reference.bases || header.digest != reference.digest) {
        return makeError<RefStats>(ErrorCode::kChecksumError,
                                   "reference digest or contig table disagrees");
    }
    for (std::size_t i = 0; i < header.contigs.size(); ++i) {
        if (header.contigs[i].first != reference.contigs[i].name ||
            header.contigs[i].second != reference.contigs[i].sequence.size()) {
            return makeError<RefStats>(ErrorCode::kFormatError,
                                       "archive/reference contig table disagrees");
        }
    }

    RefStats stats;
    stats.inputBytes = std::filesystem::file_size(archivePath);
    std::uint64_t globalChecksum = 0;
    for (;;) {
        auto block = readBlock(
            input, static_cast<std::uint32_t>(stats.blocks), reference, stats, globalChecksum);
        if (!block) {
            return makeError<RefStats>(block.error());
        }
        if (!block->has_value()) {
            break;
        }
        for (const auto& record : **block) {
            stats.outputBytes += canonicalFastqBytes(record);
            if (output != nullptr) {
                writeFastqRecord(*output, record);
            }
        }
    }
    return stats;
}

}  // namespace

auto compress(const RefCompressionRequest& request) -> Result<RefStats> {
    if (request.blockBases == 0) {
        return makeError<RefStats>(ErrorCode::kUsageError, "block size must be positive");
    }
    if (request.inputPath == request.outputPath) {
        return makeError<RefStats>(ErrorCode::kUsageError, "input and output must differ");
    }
    if (request.outputPath == request.referencePath || request.outputPath == request.pafPath ||
        request.inputPath == request.referencePath || request.inputPath == request.pafPath) {
        return makeError<RefStats>(ErrorCode::kUsageError,
                                   "FASTQ, reference and PAF paths must be distinct");
    }
    std::error_code error;
    if (std::filesystem::exists(request.outputPath, error) && !request.forceOverwrite) {
        return makeError<RefStats>(ErrorCode::kIOError,
                                   "output already exists: " + request.outputPath.string());
    }

    FQC_TRY_ASSIGN(reference, loadReference(request.referencePath));
    FQC_TRY_ASSIGN(alignments, loadPaf(request.pafPath));
    std::ifstream input(request.inputPath, std::ios::binary);
    if (!input) {
        return makeError<RefStats>(ErrorCode::kIOError,
                                   "failed to open FASTQ: " + request.inputPath.string());
    }
    std::ofstream output(request.outputPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        return makeError<RefStats>(ErrorCode::kIOError,
                                   "failed to open output: " + request.outputPath.string());
    }

    FQC_TRY(writeGlobalHeader(output, reference));
    io::FastqParser parser(input);
    RefStats stats;
    std::uint64_t globalChecksum = 0;
    for (;;) {
        std::vector<ReadRecord> block;
        std::uint64_t bases = 0;
        while (bases < request.blockBases) {
            auto record = parser.readRecord();
            if (!record) {
                return makeError<RefStats>(record.error());
            }
            if (!record->has_value()) {
                break;
            }
            bases += (*record)->sequence.size();
            block.push_back(std::move(**record));
        }
        if (block.empty()) {
            break;
        }
        auto written = writeBlock(output,
                                  static_cast<std::uint32_t>(stats.blocks),
                                  block,
                                  reference,
                                  alignments,
                                  request.maxMismatches);
        if (!written) {
            return makeError<RefStats>(written.error());
        }
        stats.blocks += 1;
        stats.records += block.size();
        stats.bases += bases;
        stats.mapped += written->second.mapped;
        stats.literal += written->second.literal;
        stats.clipped += written->second.clipped;
        stats.indel += written->second.indel;
        stats.idsBytes += written->second.idsBytes;
        stats.qualitiesBytes += written->second.qualitiesBytes;
        stats.alignmentsBytes += written->second.alignmentsBytes;
        stats.fqzBlocks += written->second.fqzBlocks;
        stats.zstdQualityBlocks += written->second.zstdQualityBlocks;
        stats.frontIdBlocks += written->second.frontIdBlocks;
        globalChecksum = format::advanceGlobalChecksum(globalChecksum, written->first);
    }

    Bytes footer;
    footer.reserve(kFooterSize);
    appendU32(footer, kFooterMagic);
    appendU32(footer, static_cast<std::uint32_t>(kFooterSize));
    appendU64(footer, stats.blocks);
    appendU64(footer, stats.records);
    appendU64(footer, stats.bases);
    appendU64(footer, globalChecksum);
    FQC_TRY(writeBytes(output, footer));
    output.flush();
    if (!output) {
        return makeError<RefStats>(ErrorCode::kIOError, "failed to flush archive");
    }
    stats.inputBytes = std::filesystem::file_size(request.inputPath);
    stats.outputBytes = std::filesystem::file_size(request.outputPath);
    return stats;
}

auto decompress(const std::filesystem::path& archivePath,
                const std::filesystem::path& referencePath,
                const std::filesystem::path& outputPath,
                bool forceOverwrite) -> Result<RefStats> {
    if (outputPath == archivePath || outputPath == referencePath) {
        return makeError<RefStats>(ErrorCode::kUsageError,
                                   "archive, reference and output paths must be distinct");
    }
    std::error_code error;
    if (std::filesystem::exists(outputPath, error) && !forceOverwrite) {
        return makeError<RefStats>(ErrorCode::kIOError,
                                   "output already exists: " + outputPath.string());
    }
    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        return makeError<RefStats>(ErrorCode::kIOError,
                                   "failed to open output: " + outputPath.string());
    }
    auto stats = decodeArchive(archivePath, referencePath, &output);
    if (!stats) {
        return stats;
    }
    output.flush();
    if (!output) {
        return makeError<RefStats>(ErrorCode::kIOError, "failed to flush FASTQ");
    }
    return stats;
}

auto verify(const std::filesystem::path& archivePath,
            const std::filesystem::path& referencePath) -> Result<RefStats> {
    return decodeArchive(archivePath, referencePath, nullptr);
}

}  // namespace fqc::tools::ref
