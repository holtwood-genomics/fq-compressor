// =============================================================================
// fq-compressor - Phase 0 Ratio Probe Implementation
// =============================================================================

#include "ratio_probe.h"

#include "fqc/io/compressed_stream.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <zlib.h>
#include <zstd.h>

#include <fmt/format.h>

namespace fqc::tools {

namespace {

constexpr std::size_t kCompressionBufferBytes = 64 * 1024;
constexpr std::string_view kProbeSchema = "fqc-ratio-probe/v1";

[[nodiscard]] constexpr auto varintSize(std::uint64_t value) noexcept -> std::size_t {
    std::size_t result = 1;
    while (value >= 0x80U) {
        value >>= 7U;
        ++result;
    }
    return result;
}

[[nodiscard]] auto zstdError(std::size_t code) -> Error {
    return Error{ErrorCode::kInternalError, fmt::format("zstd: {}", ZSTD_getErrorName(code))};
}

/// Streaming zstd byte counter. It never stores the compressed payload; only
/// the produced byte count is kept.
class ZstdCounter {
public:
    explicit ZstdCounter(int level) : level_(level) {}

    ~ZstdCounter() {
        if (context_ != nullptr) {
            ZSTD_freeCCtx(context_);
        }
    }

    ZstdCounter(const ZstdCounter&) = delete;
    auto operator=(const ZstdCounter&) -> ZstdCounter& = delete;

    [[nodiscard]] auto update(std::string_view data) -> VoidResult {
        if (finished_) {
            return makeVoidError(ErrorCode::kInternalError, "zstd counter used after finish");
        }
        FQC_TRY(ensureContext());

        std::array<char, kCompressionBufferBytes> output{};
        ZSTD_inBuffer input{data.data(), data.size(), 0};
        while (input.pos < input.size) {
            ZSTD_outBuffer frameOutput{output.data(), output.size(), 0};
            const auto result =
                ZSTD_compressStream2(context_, &frameOutput, &input, ZSTD_e_continue);
            if (ZSTD_isError(result) != 0U) {
                return std::unexpected(zstdError(result));
            }
            bytes_ += frameOutput.pos;
        }
        return {};
    }

    [[nodiscard]] auto finish() -> Result<std::uint64_t> {
        if (finished_) {
            return bytes_;
        }
        FQC_TRY(ensureContext());

        std::array<char, kCompressionBufferBytes> output{};
        ZSTD_inBuffer input{nullptr, 0, 0};
        for (;;) {
            ZSTD_outBuffer frameOutput{output.data(), output.size(), 0};
            const auto result = ZSTD_compressStream2(context_, &frameOutput, &input, ZSTD_e_end);
            if (ZSTD_isError(result) != 0U) {
                return makeError<std::uint64_t>(zstdError(result));
            }
            bytes_ += frameOutput.pos;
            if (result == 0) {
                break;
            }
        }
        finished_ = true;
        return bytes_;
    }

private:
    [[nodiscard]] auto ensureContext() -> VoidResult {
        if (context_ != nullptr) {
            return {};
        }
        context_ = ZSTD_createCCtx();
        if (context_ == nullptr) {
            return makeVoidError(ErrorCode::kInternalError, "failed to create zstd context");
        }

        const auto levelResult = ZSTD_CCtx_setParameter(context_, ZSTD_c_compressionLevel, level_);
        if (ZSTD_isError(levelResult) != 0U) {
            return std::unexpected(zstdError(levelResult));
        }
        const auto workersResult = ZSTD_CCtx_setParameter(context_, ZSTD_c_nbWorkers, 0);
        if (ZSTD_isError(workersResult) != 0U) {
            return std::unexpected(zstdError(workersResult));
        }
        return {};
    }

    ZSTD_CCtx* context_ = nullptr;
    int level_ = 19;
    std::uint64_t bytes_ = 0;
    bool finished_ = false;
};

/// Streaming gzip byte counter using the same zlib implementation as input
/// decompression. This is an in-process level-N baseline, not an external
/// `gzip` invocation with its own header metadata.
class GzipCounter {
public:
    explicit GzipCounter(int level) : level_(level) {}

    ~GzipCounter() {
        if (initialized_) {
            deflateEnd(&stream_);
        }
    }

    GzipCounter(const GzipCounter&) = delete;
    auto operator=(const GzipCounter&) -> GzipCounter& = delete;

    [[nodiscard]] auto update(std::string_view data) -> VoidResult {
        if (finished_) {
            return makeVoidError(ErrorCode::kInternalError, "gzip counter used after finish");
        }
        FQC_TRY(ensureInitialized());

        std::array<Bytef, kCompressionBufferBytes> output{};
        while (!data.empty()) {
            const auto chunk =
                std::min(data.size(), static_cast<std::size_t>(std::numeric_limits<uInt>::max()));
            stream_.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data.data()));
            stream_.avail_in = static_cast<uInt>(chunk);

            while (stream_.avail_in > 0) {
                stream_.next_out = output.data();
                stream_.avail_out = static_cast<uInt>(output.size());
                const int result = deflate(&stream_, Z_NO_FLUSH);
                if (result != Z_OK) {
                    return makeVoidError(ErrorCode::kInternalError,
                                         fmt::format("zlib deflate failed ({})", result));
                }
                bytes_ += output.size() - stream_.avail_out;
            }
            data.remove_prefix(chunk);
        }
        return {};
    }

    [[nodiscard]] auto finish() -> Result<std::uint64_t> {
        if (finished_) {
            return bytes_;
        }
        FQC_TRY(ensureInitialized());

        std::array<Bytef, kCompressionBufferBytes> output{};
        int result = Z_OK;
        do {
            stream_.next_out = output.data();
            stream_.avail_out = static_cast<uInt>(output.size());
            result = deflate(&stream_, Z_FINISH);
            bytes_ += output.size() - stream_.avail_out;
        } while (result == Z_OK);

        if (result != Z_STREAM_END) {
            return makeError<std::uint64_t>(ErrorCode::kInternalError,
                                            fmt::format("zlib deflate finish failed ({})", result));
        }
        finished_ = true;
        return bytes_;
    }

private:
    [[nodiscard]] auto ensureInitialized() -> VoidResult {
        if (initialized_) {
            return {};
        }
        stream_ = {};
        const int result =
            deflateInit2(&stream_, level_, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
        if (result != Z_OK) {
            return makeVoidError(ErrorCode::kInternalError,
                                 fmt::format("zlib deflateInit2 failed ({})", result));
        }
        initialized_ = true;
        return {};
    }

    z_stream stream_{};
    int level_ = 6;
    std::uint64_t bytes_ = 0;
    bool initialized_ = false;
    bool finished_ = false;
};

struct ParsedLine {
    std::string_view content;
    std::uint64_t lineEndingBytes = 0;
    bool hasNewline = false;
};

/// Byte-preserving FASTQ line reader. It keeps CRLF attribution separate from
/// field content and feeds the whole-file gzip/zstd counters.
class FastqByteReader {
public:
    FastqByteReader(std::istream& input, const RatioProbeOptions& options, RatioProbeStats& stats)
        : input_(input), stats_(stats), zstd_(options.zstdLevel), gzip_(options.gzipLevel) {}

    [[nodiscard]] auto next() -> Result<std::optional<ParsedLine>> {
        if (!std::getline(input_, line_)) {
            if (input_.bad()) {
                return makeError<std::optional<ParsedLine>>(
                    ErrorCode::kIOError, "input stream read error while parsing FASTQ");
            }
            return std::optional<ParsedLine>{};
        }

        const bool hasNewline = !input_.eof();
        FQC_TRY(zstd_.update(line_));
        FQC_TRY(gzip_.update(line_));
        if (hasNewline) {
            FQC_TRY(zstd_.update("\n"));
            FQC_TRY(gzip_.update("\n"));
        }

        std::string_view content(line_);
        std::uint64_t lineEndingBytes = 0;
        if (hasNewline) {
            if (!content.empty() && content.back() == '\r') {
                content.remove_suffix(1);
                lineEndingBytes = 2;
                ++stats_.crlfLineEndings;
            } else {
                lineEndingBytes = 1;
                ++stats_.lfLineEndings;
            }
        } else {
            ++stats_.finalLineWithoutNewline;
        }

        ++stats_.lines;
        stats_.totalBytes += content.size() + lineEndingBytes;
        return ParsedLine{content, lineEndingBytes, hasNewline};
    }

    [[nodiscard]] auto required(std::string_view context) -> Result<ParsedLine> {
        auto line = next();
        if (!line) {
            return makeError<ParsedLine>(line.error());
        }
        if (!line->has_value()) {
            return makeError<ParsedLine>(ErrorCode::kFormatError,
                                         fmt::format("unexpected EOF while reading {}", context));
        }
        return **line;
    }

    void account(StreamBytes& stream, const ParsedLine& line) {
        stream.content += line.content.size();
        stream.lineEndings += line.lineEndingBytes;
    }

    [[nodiscard]] auto finish() -> VoidResult {
        auto zstdBytes = zstd_.finish();
        if (!zstdBytes) {
            return std::unexpected(zstdBytes.error());
        }
        stats_.zstdBytes = *zstdBytes;

        auto gzipBytes = gzip_.finish();
        if (!gzipBytes) {
            return std::unexpected(gzipBytes.error());
        }
        stats_.gzipBytes = *gzipBytes;
        return {};
    }

private:
    std::istream& input_;
    RatioProbeStats& stats_;
    ZstdCounter zstd_;
    GzipCounter gzip_;
    std::string line_;
};

[[nodiscard]] auto detectInputCompression(const std::filesystem::path& path) -> std::string {
    if (path == "-") {
        return "auto";
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return "unknown";
    }

    std::array<std::uint8_t, 8> magic{};
    input.read(reinterpret_cast<char*>(magic.data()), static_cast<std::streamsize>(magic.size()));
    const auto bytesRead = static_cast<std::size_t>(input.gcount());
    auto format = io::detectCompressionFormat({magic.data(), bytesRead});
    if (format == io::CompressionFormat::kNone && bytesRead > 0 && bytesRead < 2) {
        format = io::detectCompressionFormatFromExtension(path);
    }
    return std::string(io::compressionFormatName(format));
}

auto countSequence(RatioProbeStats& stats, std::string_view sequence) -> void {
    stats.bases += sequence.size();
    if (sequence.empty()) {
        ++stats.emptySequences;
    }

    std::uint64_t exceptions = 0;
    for (const char base : sequence) {
        const bool isAcgt = base == 'A' || base == 'C' || base == 'G' || base == 'T' ||
            base == 'a' || base == 'c' || base == 'g' || base == 't';
        const bool isPackedAcgt = base == 'A' || base == 'C' || base == 'G' || base == 'T';
        const bool isN = base == 'N' || base == 'n';
        if (isAcgt) {
            ++stats.acgtBases;
        } else if (isN) {
            ++stats.nBases;
        } else {
            ++stats.otherBases;
        }
        if (!isPackedAcgt) {
            ++exceptions;
        }
    }

    const auto packedBytes = sequence.size() / 4 + (sequence.size() % 4 == 0 ? 0 : 1);
    stats.packedMainBytes += varintSize(sequence.size()) + packedBytes;
    stats.packedExceptionBytes += varintSize(exceptions);

    if (exceptions == 0) {
        return;
    }
    std::size_t previous = 0;
    std::size_t emitted = 0;
    for (std::size_t index = 0; index < sequence.size(); ++index) {
        const char base = sequence[index];
        const bool isPackedAcgt = base == 'A' || base == 'C' || base == 'G' || base == 'T';
        if (isPackedAcgt) {
            continue;
        }
        const auto delta = emitted == 0 ? index : index - previous;
        stats.packedExceptionBytes += varintSize(delta) + 1;
        previous = index;
        ++emitted;
    }
}

auto countQuality(RatioProbeStats& stats, std::string_view quality) -> void {
    for (const char value : quality) {
        const auto byte = static_cast<unsigned char>(value);
        if (byte < 33 || byte > 126) {
            ++stats.invalidQualityBytes;
            continue;
        }
        ++stats.qualityHistogram[byte];
    }
}

[[nodiscard]] auto jsonEscape(std::string_view value) -> std::string {
    std::string output;
    for (const char raw : value) {
        const auto byte = static_cast<unsigned char>(raw);
        switch (byte) {
            case '"':
                output += "\\\"";
                break;
            case '\\':
                output += "\\\\";
                break;
            case '\b':
                output += "\\b";
                break;
            case '\f':
                output += "\\f";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            default:
                if (byte < 0x20) {
                    output += fmt::format("\\u{:04x}", byte);
                } else {
                    output += raw;
                }
                break;
        }
    }
    return output;
}

[[nodiscard]] auto jsonString(std::string_view value) -> std::string {
    return "\"" + jsonEscape(value) + "\"";
}

[[nodiscard]] auto ratio(double numerator, double denominator) -> double {
    return denominator == 0 ? 0.0 : numerator / denominator;
}

[[nodiscard]] auto streamJson(const StreamBytes& stream) -> std::string {
    return fmt::format("{{\"content\": {}, \"line_endings\": {}, \"total\": {}}}",
                       stream.content,
                       stream.lineEndings,
                       stream.total());
}

}  // namespace

auto RatioProbeStats::qualitySymbols() const noexcept -> std::size_t {
    std::size_t symbols = 0;
    for (const auto count : qualityHistogram) {
        if (count != 0) {
            ++symbols;
        }
    }
    return symbols;
}

auto RatioProbeStats::validQualityBytes() const noexcept -> std::uint64_t {
    return quality.content >= invalidQualityBytes ? quality.content - invalidQualityBytes : 0;
}

auto RatioProbeStats::meanQuality() const noexcept -> double {
    const auto valid = validQualityBytes();
    if (valid == 0) {
        return 0.0;
    }
    double sum = 0.0;
    for (std::size_t symbol = 0; symbol < qualityHistogram.size(); ++symbol) {
        const auto count = qualityHistogram[symbol];
        if (count != 0) {
            sum += static_cast<double>(static_cast<int>(symbol) - 33) * static_cast<double>(count);
        }
    }
    return sum / static_cast<double>(valid);
}

auto analyzeFastq(std::istream& input,
                  const RatioProbeOptions& options,
                  RatioProbeStats& stats) -> VoidResult {
    if (options.zstdLevel < 1 || options.zstdLevel > ZSTD_maxCLevel()) {
        return makeVoidError(ErrorCode::kUsageError, "zstd level is out of range");
    }
    if (options.gzipLevel < 0 || options.gzipLevel > 9) {
        return makeVoidError(ErrorCode::kUsageError, "gzip level is out of range");
    }

    stats.zstdLevel = options.zstdLevel;
    stats.gzipLevel = options.gzipLevel;

    FastqByteReader reader(input, options, stats);
    ZstdCounter idZstd(options.zstdLevel);
    ZstdCounter qualityZstd(options.zstdLevel);

    for (;;) {
        auto first = reader.next();
        if (!first) {
            return std::unexpected(first.error());
        }
        if (!first->has_value()) {
            break;
        }

        if ((**first).content.empty()) {
            reader.account(stats.blank, **first);
            ++stats.blankLines;
            continue;
        }

        const auto headerLine = **first;
        if (headerLine.content.front() != '@') {
            return makeVoidError(
                ErrorCode::kFormatError,
                fmt::format("invalid FASTQ at line {}: expected '@'", stats.lines));
        }
        reader.account(stats.header, headerLine);

        const std::string headerText(headerLine.content.substr(1));
        if (headerText.empty()) {
            return makeVoidError(
                ErrorCode::kFormatError,
                fmt::format("invalid FASTQ at line {}: empty read ID", stats.lines));
        }
        const auto space = headerText.find(' ');
        const auto id = std::string_view(headerText).substr(0, space);
        if (id.empty()) {
            return makeVoidError(
                ErrorCode::kFormatError,
                fmt::format("invalid FASTQ at line {}: empty read ID", stats.lines));
        }
        stats.idBytes += id.size();
        if (space != std::string::npos) {
            stats.commentBytes += headerText.size() - space;
        }
        FQC_TRY(idZstd.update(headerText));
        FQC_TRY(idZstd.update("\n"));

        auto sequenceLine = reader.required("sequence");
        if (!sequenceLine) {
            return std::unexpected(sequenceLine.error());
        }
        reader.account(stats.sequence, *sequenceLine);
        const auto sequenceLength = sequenceLine->content.size();
        countSequence(stats, sequenceLine->content);

        auto plusLine = reader.required("'+' line");
        if (!plusLine) {
            return std::unexpected(plusLine.error());
        }
        if (plusLine->content.empty() || plusLine->content.front() != '+') {
            return makeVoidError(
                ErrorCode::kFormatError,
                fmt::format("invalid FASTQ at line {}: expected '+'", stats.lines));
        }
        reader.account(stats.plus, *plusLine);
        const auto plusText = plusLine->content.substr(1);
        if (plusText.empty()) {
            ++stats.plusEmpty;
        } else if (plusText == headerText) {
            ++stats.plusMatchesHeader;
        } else {
            ++stats.plusLiteral;
        }

        auto qualityLine = reader.required("quality");
        if (!qualityLine) {
            return std::unexpected(qualityLine.error());
        }
        if (qualityLine->content.size() != sequenceLength) {
            return makeVoidError(
                ErrorCode::kFormatError,
                fmt::format("invalid FASTQ at line {}: quality length does not match sequence",
                            stats.lines));
        }
        reader.account(stats.quality, *qualityLine);
        countQuality(stats, qualityLine->content);
        FQC_TRY(qualityZstd.update(qualityLine->content));
        FQC_TRY(qualityZstd.update("\n"));

        ++stats.records;
    }

    FQC_TRY(reader.finish());

    auto idBytes = idZstd.finish();
    if (!idBytes) {
        return std::unexpected(idBytes.error());
    }
    stats.idZstdBytes = *idBytes;

    auto qualityBytes = qualityZstd.finish();
    if (!qualityBytes) {
        return std::unexpected(qualityBytes.error());
    }
    stats.qualityZstdBytes = *qualityBytes;

    stats.packedSequenceBytes =
        varintSize(stats.records) + stats.packedMainBytes + stats.packedExceptionBytes;

    const auto streamTotal = stats.header.total() + stats.sequence.total() + stats.plus.total() +
        stats.quality.total() + stats.blank.total();
    if (streamTotal != stats.totalBytes) {
        return makeVoidError(ErrorCode::kInternalError,
                             "ratio probe stream accounting does not match raw bytes");
    }
    return {};
}

auto analyzeFile(const std::filesystem::path& path,
                 const RatioProbeOptions& options) -> Result<RatioProbeStats> {
    RatioProbeStats stats;
    stats.inputPath = path.string();
    stats.inputCompression = detectInputCompression(path);

    if (path != "-") {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (!error) {
            stats.inputFileBytes = size;
            stats.inputFileBytesKnown = true;
        }
    }

    auto input = io::openInputFile(path);
    if (!input) {
        return makeError<RatioProbeStats>(input.error());
    }
    FQC_TRY(analyzeFastq(**input, options, stats));
    return stats;
}

auto toJson(const RatioProbeStats& stats) -> std::string {
    const auto rawBytes = static_cast<double>(stats.totalBytes);
    const auto gzipBytes = static_cast<double>(stats.gzipBytes);
    const auto zstdBytes = static_cast<double>(stats.zstdBytes);
    const auto bases = static_cast<double>(stats.bases);
    const auto qualityBits = static_cast<double>(stats.qualityZstdBytes) * 8.0;

    const auto inputBytes =
        stats.inputFileBytesKnown ? std::to_string(stats.inputFileBytes) : std::string("null");
    const auto warnings = stats.invalidQualityBytes == 0
        ? std::string("[]")
        : fmt::format("[\"quality bytes outside printable ASCII: {}\"]", stats.invalidQualityBytes);

    return fmt::format(
        "{{\n"
        "  \"schema\": \"{}\",\n"
        "  \"input\": {{\"path\": {}, \"bytes\": {}, \"bytes_known\": {}, "
        "\"compression\": {}}},\n"
        "  \"records\": {},\n"
        "  \"bases\": {},\n"
        "  \"acgt_bases\": {},\n"
        "  \"n_bases\": {},\n"
        "  \"other_bases\": {},\n"
        "  \"empty_sequences\": {},\n"
        "  \"lines\": {},\n"
        "  \"blank_lines\": {},\n"
        "  \"line_endings\": {{\"lf\": {}, \"crlf\": {}, \"final_without_newline\": {}}},\n"
        "  \"raw_streams\": {{\n"
        "    \"header\": {},\n"
        "    \"sequence\": {},\n"
        "    \"plus\": {},\n"
        "    \"quality\": {},\n"
        "    \"blank\": {}\n"
        "  }},\n"
        "  \"id_comment\": {{\"id_bytes\": {}, \"comment_bytes\": {}}},\n"
        "  \"plus\": {{\"empty\": {}, \"matches_header\": {}, \"literal\": {}}},\n"
        "  \"quality\": {{\"bytes\": {}, \"valid_bytes\": {}, \"symbols\": {}, "
        "\"mean_q\": {:.6f}, \"invalid_bytes\": {}}},\n"
        "  \"packed_sequence\": {{\"main_bytes\": {}, \"exception_bytes\": {}, "
        "\"total_bytes\": {}}},\n"
        "  \"baselines\": {{\n"
        "    \"gzip\": {{\"level\": {}, \"implementation\": \"zlib\", \"bytes\": {}}},\n"
        "    \"zstd\": {{\"level\": {}, \"bytes\": {}}},\n"
        "    \"id_zstd\": {{\"level\": {}, \"bytes\": {}}},\n"
        "    \"quality_zstd\": {{\"level\": {}, \"bytes\": {}}}\n"
        "  }},\n"
        "  \"ratios\": {{\"raw_over_gzip\": {:.6f}, \"raw_over_zstd\": {:.6f}, "
        "\"gzip_over_zstd\": {:.6f}, \"raw_bits_per_base\": {:.6f}, "
        "\"quality_bits_per_base\": {:.6f}}},\n"
        "  \"warnings\": {}\n"
        "}}\n",
        kProbeSchema,
        jsonString(stats.inputPath),
        inputBytes,
        stats.inputFileBytesKnown ? "true" : "false",
        jsonString(stats.inputCompression),
        stats.records,
        stats.bases,
        stats.acgtBases,
        stats.nBases,
        stats.otherBases,
        stats.emptySequences,
        stats.lines,
        stats.blankLines,
        stats.lfLineEndings,
        stats.crlfLineEndings,
        stats.finalLineWithoutNewline,
        streamJson(stats.header),
        streamJson(stats.sequence),
        streamJson(stats.plus),
        streamJson(stats.quality),
        streamJson(stats.blank),
        stats.idBytes,
        stats.commentBytes,
        stats.plusEmpty,
        stats.plusMatchesHeader,
        stats.plusLiteral,
        stats.quality.content,
        stats.validQualityBytes(),
        stats.qualitySymbols(),
        stats.meanQuality(),
        stats.invalidQualityBytes,
        stats.packedMainBytes,
        stats.packedExceptionBytes,
        stats.packedSequenceBytes,
        stats.gzipLevel,
        stats.gzipBytes,
        stats.zstdLevel,
        stats.zstdBytes,
        stats.zstdLevel,
        stats.idZstdBytes,
        stats.zstdLevel,
        stats.qualityZstdBytes,
        ratio(rawBytes, gzipBytes),
        ratio(rawBytes, zstdBytes),
        ratio(gzipBytes, zstdBytes),
        ratio(rawBytes * 8.0, bases),
        ratio(qualityBits, bases),
        warnings);
}

auto toText(const RatioProbeStats& stats) -> std::string {
    const auto rawBytes = static_cast<double>(stats.totalBytes);
    const auto gzipBytes = static_cast<double>(stats.gzipBytes);
    const auto zstdBytes = static_cast<double>(stats.zstdBytes);
    const auto bases = static_cast<double>(stats.bases);
    const auto qualityBits = static_cast<double>(stats.qualityZstdBytes) * 8.0;

    return fmt::format(
        "input={}\n"
        "compression={}\n"
        "file_bytes={}\n"
        "raw_bytes={}\n"
        "records={}\n"
        "bases={}\n"
        "acgt_bases={}\n"
        "n_bases={}\n"
        "other_bases={}\n"
        "empty_sequences={}\n"
        "lines={}\n"
        "blank_lines={}\n"
        "line_endings: lf={} crlf={} final_without_newline={}\n"
        "streams: header={} sequence={} plus={} quality={} blank={}\n"
        "id_comment: id={} comment={}\n"
        "plus: empty={} matches_header={} literal={}\n"
        "quality: bytes={} valid={} symbols={} mean_q={:.6f} invalid={}\n"
        "packed_sequence: main={} exceptions={} total={}\n"
        "baselines: gzip_zlib{}({}) zstd{}({}) id_zstd{}({}) quality_zstd{}({})\n"
        "ratios: raw/gzip={:.6f} raw/zstd={:.6f} gzip/zstd={:.6f} "
        "raw_bpb={:.6f} quality_bpb={:.6f}\n",
        stats.inputPath,
        stats.inputCompression,
        stats.inputFileBytesKnown ? std::to_string(stats.inputFileBytes) : "unknown",
        stats.totalBytes,
        stats.records,
        stats.bases,
        stats.acgtBases,
        stats.nBases,
        stats.otherBases,
        stats.emptySequences,
        stats.lines,
        stats.blankLines,
        stats.lfLineEndings,
        stats.crlfLineEndings,
        stats.finalLineWithoutNewline,
        stats.header.total(),
        stats.sequence.total(),
        stats.plus.total(),
        stats.quality.total(),
        stats.blank.total(),
        stats.idBytes,
        stats.commentBytes,
        stats.plusEmpty,
        stats.plusMatchesHeader,
        stats.plusLiteral,
        stats.quality.content,
        stats.validQualityBytes(),
        stats.qualitySymbols(),
        stats.meanQuality(),
        stats.invalidQualityBytes,
        stats.packedMainBytes,
        stats.packedExceptionBytes,
        stats.packedSequenceBytes,
        stats.gzipLevel,
        stats.gzipBytes,
        stats.zstdLevel,
        stats.zstdBytes,
        stats.zstdLevel,
        stats.idZstdBytes,
        stats.zstdLevel,
        stats.qualityZstdBytes,
        ratio(rawBytes, gzipBytes),
        ratio(rawBytes, zstdBytes),
        ratio(gzipBytes, zstdBytes),
        ratio(rawBytes * 8.0, bases),
        ratio(qualityBits, bases));
}

}  // namespace fqc::tools
