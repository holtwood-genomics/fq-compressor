// =============================================================================
// fq-compressor - max-ratio quality codec probe
// =============================================================================
// Development-only experiment for the max-ratio track. It compares htscodecs
// fqzcomp against zstd on the quality stream only. It does not write an
// archive and is not part of the product CLI.
// =============================================================================

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "fqzcomp_qual.h"
#include <zstd.h>

namespace {

struct Options {
    std::size_t blockRecords = 25000;
    int strategy = -1;  // -1 = try 0 and 1 per block, pick the smaller
    int zstdLevel = 19;
    bool json = false;
};

struct BlockResult {
    std::size_t records = 0;
    std::size_t bases = 0;
    std::size_t zstdBytes = 0;
    std::size_t fqzBytes = 0;
    int strategy = -1;
};

struct ProbeResult {
    std::size_t records = 0;
    std::size_t bases = 0;
    std::size_t zstdBytes = 0;
    std::size_t fqzBytes = 0;
    std::vector<BlockResult> blocks;
};

struct QualityBlock {
    std::vector<std::uint32_t> lengths;
    std::vector<unsigned char> values;
    std::size_t bases = 0;
};

class FastqQualityReader {
public:
    explicit FastqQualityReader(std::istream& input) : input_(input) {}

    [[nodiscard]] auto next(QualityBlock& block) -> bool {
        if (!std::getline(input_, header_)) {
            return false;
        }
        if (!std::getline(input_, sequence_) || !std::getline(input_, plus_) ||
            !std::getline(input_, quality_)) {
            throw std::runtime_error("truncated FASTQ record");
        }
        trimCr(header_);
        trimCr(sequence_);
        trimCr(plus_);
        trimCr(quality_);

        if (header_.empty() || header_[0] != '@') {
            throw std::runtime_error("FASTQ header does not start with '@'");
        }
        if (plus_.empty() || plus_[0] != '+') {
            throw std::runtime_error("FASTQ plus line does not start with '+'");
        }
        if (sequence_.size() != quality_.size()) {
            throw std::runtime_error("sequence and quality lengths differ");
        }

        const auto length = static_cast<std::uint32_t>(quality_.size());
        block.lengths.push_back(length);
        block.bases += length;
        for (const char value : quality_) {
            const auto byte = static_cast<unsigned char>(value);
            if (byte < 33 || byte > 126) {
                throw std::runtime_error("quality byte outside printable Phred+33 range");
            }
            block.values.push_back(static_cast<unsigned char>(byte - 33));
        }
        return true;
    }

private:
    static void trimCr(std::string& line) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
    }

    std::istream& input_;
    std::string header_;
    std::string sequence_;
    std::string plus_;
    std::string quality_;
};

[[nodiscard]] auto zstdSize(const std::vector<unsigned char>& input, int level) -> std::size_t {
    if (input.empty()) {
        return 0;
    }
    const auto bound = ZSTD_compressBound(input.size());
    std::vector<char> output(bound);
    const auto written =
        ZSTD_compress(output.data(), output.size(), input.data(), input.size(), level);
    if (ZSTD_isError(written) != 0U) {
        throw std::runtime_error(std::string("zstd: ") + ZSTD_getErrorName(written));
    }
    return written;
}

[[nodiscard]] auto compressFqz(const QualityBlock& block, int strategy) -> std::size_t {
    if (block.values.empty()) {
        return 0;
    }

    fqz_slice slice{};
    slice.num_records = static_cast<int>(block.lengths.size());
    slice.len = const_cast<std::uint32_t*>(block.lengths.data());
    std::vector<std::uint32_t> flags(block.lengths.size(), 0);
    slice.flags = flags.data();

    std::size_t compressedSize = 0;
    auto* compressed =
        fqz_compress(4,
                     &slice,
                     reinterpret_cast<char*>(const_cast<unsigned char*>(block.values.data())),
                     block.values.size(),
                     &compressedSize,
                     strategy,
                     nullptr);
    if (compressed == nullptr) {
        throw std::runtime_error("fqzcomp compression failed");
    }

    std::size_t restoredSize = 0;
    std::vector<int> restoredLengths(block.lengths.size());
    auto* restored = fqz_decompress(compressed,
                                    compressedSize,
                                    &restoredSize,
                                    restoredLengths.data(),
                                    static_cast<int>(restoredLengths.size()));
    const bool ok = restored != nullptr && restoredSize == block.values.size() &&
        std::memcmp(restored, block.values.data(), block.values.size()) == 0;
    std::free(compressed);
    std::free(restored);
    if (!ok) {
        throw std::runtime_error("fqzcomp round-trip mismatch");
    }
    for (std::size_t i = 0; i < block.lengths.size(); ++i) {
        if (restoredLengths[i] != static_cast<int>(block.lengths[i])) {
            throw std::runtime_error("fqzcomp restored length mismatch");
        }
    }
    return compressedSize;
}

[[nodiscard]] auto probeBlock(const QualityBlock& block, const Options& options) -> BlockResult {
    BlockResult result;
    result.records = block.lengths.size();
    result.bases = block.bases;
    result.zstdBytes = zstdSize(block.values, options.zstdLevel);

    if (options.strategy >= 0) {
        result.strategy = options.strategy;
        result.fqzBytes = compressFqz(block, options.strategy);
        return result;
    }

    const auto first = compressFqz(block, 0);
    const auto second = compressFqz(block, 1);
    if (second < first) {
        result.strategy = 1;
        result.fqzBytes = second;
    } else {
        result.strategy = 0;
        result.fqzBytes = first;
    }
    return result;
}

void flushBlock(QualityBlock& block, const Options& options, ProbeResult& result) {
    if (block.lengths.empty()) {
        return;
    }
    const auto blockResult = probeBlock(block, options);
    result.records += blockResult.records;
    result.bases += blockResult.bases;
    result.zstdBytes += blockResult.zstdBytes;
    result.fqzBytes += blockResult.fqzBytes;
    result.blocks.push_back(blockResult);
    block = {};
}

[[nodiscard]] auto probe(std::istream& input, const Options& options) -> ProbeResult {
    ProbeResult result;
    QualityBlock block;
    FastqQualityReader reader(input);
    while (reader.next(block)) {
        if (options.blockRecords != 0 && block.lengths.size() >= options.blockRecords) {
            flushBlock(block, options, result);
        }
    }
    flushBlock(block, options, result);
    return result;
}

void printText(const ProbeResult& result, const Options& options) {
    std::cout << "schema=fqc-quality-probe/v1\n";
    std::cout << "block_records=" << options.blockRecords << '\n';
    std::cout << "zstd_level=" << options.zstdLevel << '\n';
    std::cout << "records=" << result.records << '\n';
    std::cout << "bases=" << result.bases << '\n';
    std::cout << "zstd_bytes=" << result.zstdBytes << '\n';
    std::cout << "fqz_bytes=" << result.fqzBytes << '\n';
    std::cout << "fqz_strategy="
              << (options.strategy < 0 ? "adaptive-0-1" : std::to_string(options.strategy)) << '\n';
    if (result.zstdBytes != 0) {
        const auto saved = 100.0 * static_cast<double>(result.zstdBytes - result.fqzBytes) /
            static_cast<double>(result.zstdBytes);
        std::cout << "fqz_vs_zstd_percent=" << saved << '\n';
    }
    std::cout << "blocks=" << result.blocks.size() << '\n';
}

void printJson(const ProbeResult& result, const Options& options) {
    std::cout << "{\n";
    std::cout << "  \"schema\": \"fqc-quality-probe/v1\",\n";
    std::cout << "  \"block_records\": " << options.blockRecords << ",\n";
    std::cout << "  \"zstd_level\": " << options.zstdLevel << ",\n";
    std::cout << "  \"records\": " << result.records << ",\n";
    std::cout << "  \"bases\": " << result.bases << ",\n";
    std::cout << "  \"zstd_bytes\": " << result.zstdBytes << ",\n";
    std::cout << "  \"fqz_bytes\": " << result.fqzBytes << ",\n";
    std::cout << "  \"fqz_strategy\": \""
              << (options.strategy < 0 ? "adaptive-0-1" : std::to_string(options.strategy))
              << "\",\n";
    std::cout << "  \"blocks\": [";
    for (std::size_t i = 0; i < result.blocks.size(); ++i) {
        const auto& block = result.blocks[i];
        if (i != 0) {
            std::cout << ", ";
        }
        std::cout << "{\"records\": " << block.records << ", \"bases\": " << block.bases
                  << ", \"zstd_bytes\": " << block.zstdBytes
                  << ", \"fqz_bytes\": " << block.fqzBytes << ", \"strategy\": " << block.strategy
                  << "}";
    }
    std::cout << "]\n}\n";
}

void usage(std::string_view program) {
    std::cerr << "usage: " << program
              << " <reads.fastq> [--block-records N] [--strategy 0|1] "
                 "[--zstd-level N] [--json]\n"
              << "  --block-records 0 means one block for the whole file\n";
}

}  // namespace

auto main(int argc, char** argv) -> int {
    try {
        if (argc < 2) {
            usage(argv[0]);
            return 2;
        }

        Options options;
        std::string path;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--json") {
                options.json = true;
            } else if (arg == "--help" || arg == "-h") {
                usage(argv[0]);
                return 0;
            } else if (arg == "--block-records" && i + 1 < argc) {
                options.blockRecords = std::strtoull(argv[++i], nullptr, 10);
            } else if (arg == "--strategy" && i + 1 < argc) {
                options.strategy = std::atoi(argv[++i]);
                if (options.strategy < 0 || options.strategy > 3) {
                    throw std::runtime_error("--strategy must be between 0 and 3");
                }
            } else if (arg == "--zstd-level" && i + 1 < argc) {
                options.zstdLevel = std::atoi(argv[++i]);
                if (options.zstdLevel < 1 || options.zstdLevel > 22) {
                    throw std::runtime_error("--zstd-level must be between 1 and 22");
                }
            } else if (path.empty()) {
                path = arg;
            } else {
                throw std::runtime_error("unexpected argument: " + std::string(arg));
            }
        }
        if (path.empty()) {
            usage(argv[0]);
            return 2;
        }

        std::ifstream input(path, std::ios::binary);
        if (!input) {
            throw std::runtime_error("cannot open input: " + path);
        }
        const auto result = probe(input, options);
        if (options.json) {
            printJson(result, options);
        } else {
            printText(result, options);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
