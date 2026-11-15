// =============================================================================
// fq-compressor - Reference-Backed Archive Tests
// =============================================================================

#include "ref_archive.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

namespace fqc::tools::ref::test {

namespace {

struct TempDir {
    std::filesystem::path path;

    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

[[nodiscard]] auto makeTempDir() -> TempDir {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    TempDir result{std::filesystem::temp_directory_path() /
                   ("fqc_ref_archive_test_" + std::to_string(stamp))};
    std::filesystem::create_directories(result.path);
    return result;
}

void writeFile(const std::filesystem::path& path, std::string_view content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
}

[[nodiscard]] auto readFile(const std::filesystem::path& path) -> std::string {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[nodiscard]] auto referenceSequence() -> std::string {
    std::string sequence;
    for (int i = 0; i < 200; ++i) {
        sequence.push_back("ACGT"[i % 4]);
    }
    return sequence;
}

[[nodiscard]] auto reverseComplement(std::string_view sequence) -> std::string {
    std::string result;
    for (auto it = sequence.rbegin(); it != sequence.rend(); ++it) {
        switch (*it) {
            case 'A':
                result.push_back('T');
                break;
            case 'C':
                result.push_back('G');
                break;
            case 'G':
                result.push_back('C');
                break;
            default:
                result.push_back('A');
                break;
        }
    }
    return result;
}

struct Fixture {
    std::string reference;
    std::string fastq;
    std::string paf;
};

[[nodiscard]] auto makeFixture() -> Fixture {
    const auto reference = referenceSequence();
    const std::string quality(20, 'I');
    std::string fastq;
    std::string paf;

    const auto add = [&](std::string_view id, std::string_view sequence, std::string_view pafLine) {
        fastq += '@';
        fastq += id;
        fastq += '\n';
        fastq += sequence;
        fastq += "\n+\n";
        fastq += quality;
        fastq += '\n';
        if (!pafLine.empty()) {
            paf += pafLine;
            paf += '\n';
        }
    };

    add("q1",
        reference.substr(0, 20),
        "q1\t20\t0\t20\t+\tchr1\t200\t0\t20\t20\t20\t60\ttp:A:P\tNM:i:0\tcg:Z:20M");
    add("q2",
        reverseComplement(std::string_view(reference).substr(40, 20)),
        "q2\t20\t0\t20\t-\tchr1\t200\t40\t60\t20\t20\t60\ttp:A:P\tNM:i:0\tcg:Z:20M");
    add("q3", "NNNNACGTACGTACGTACGT", "");
    add("q4",
        reference.substr(80, 20),
        "q4\t20\t0\t20\t+\tchr1\t200\t80\t100\t20\t20\t60\ttp:A:P\tNM:i:0\tcg:Z:10M1I9M");

    auto mismatch = reference.substr(100, 20);
    mismatch[5] = mismatch[5] == 'A' ? 'C' : 'A';
    add("q5",
        mismatch,
        "q5\t20\t0\t20\t+\tchr1\t200\t100\t120\t20\t20\t60\ttp:A:P\tNM:i:1\tcg:Z:20M");

    return {reference, fastq, paf};
}

}  // namespace

TEST(RefArchiveTest, RoundTripsMappedAndLiteralReads) {
    auto dir = makeTempDir();
    const auto fixture = makeFixture();
    const auto referencePath = dir.path / "reference.fa";
    const auto inputPath = dir.path / "input.fastq";
    const auto pafPath = dir.path / "input.paf";
    const auto archivePath = dir.path / "output.fqmr";
    const auto restoredPath = dir.path / "restored.fastq";

    writeFile(referencePath, ">chr1 test reference\n" + fixture.reference + "\n");
    writeFile(inputPath, fixture.fastq);
    writeFile(pafPath, fixture.paf);

    const auto compressed = compress({.inputPath = inputPath,
                                      .pafPath = pafPath,
                                      .referencePath = referencePath,
                                      .outputPath = archivePath,
                                      .blockBases = 1000,
                                      .maxMismatches = 8,
                                      .forceOverwrite = true});
    ASSERT_TRUE(compressed) << compressed.error().message;
    EXPECT_EQ(compressed->records, 5U);
    EXPECT_EQ(compressed->mapped, 3U);
    EXPECT_EQ(compressed->literal, 2U);

    const auto verified = verify(archivePath, referencePath);
    ASSERT_TRUE(verified) << verified.error().message;
    EXPECT_EQ(verified->records, 5U);
    EXPECT_EQ(verified->mapped, 3U);
    EXPECT_EQ(verified->literal, 2U);

    const auto decompressed = decompress(archivePath, referencePath, restoredPath);
    ASSERT_TRUE(decompressed) << decompressed.error().message;
    EXPECT_EQ(readFile(restoredPath), fixture.fastq);
}

TEST(RefArchiveTest, RejectsReferenceDigestMismatch) {
    auto dir = makeTempDir();
    const auto fixture = makeFixture();
    const auto referencePath = dir.path / "reference.fa";
    const auto inputPath = dir.path / "input.fastq";
    const auto pafPath = dir.path / "input.paf";
    const auto archivePath = dir.path / "output.fqmr";

    writeFile(referencePath, ">chr1\n" + fixture.reference + "\n");
    writeFile(inputPath, fixture.fastq);
    writeFile(pafPath, fixture.paf);
    const auto compressed = compress({.inputPath = inputPath,
                                      .pafPath = pafPath,
                                      .referencePath = referencePath,
                                      .outputPath = archivePath,
                                      .blockBases = 1000,
                                      .maxMismatches = 8,
                                      .forceOverwrite = true});
    ASSERT_TRUE(compressed) << compressed.error().message;

    auto changed = fixture.reference;
    changed[0] = changed[0] == 'A' ? 'C' : 'A';
    writeFile(referencePath, ">chr1\n" + changed + "\n");
    const auto verified = verify(archivePath, referencePath);
    ASSERT_FALSE(verified);
    EXPECT_EQ(verified.error().code, ErrorCode::kChecksumError);
}

TEST(RefArchiveTest, RejectsTruncatedArchive) {
    auto dir = makeTempDir();
    const auto fixture = makeFixture();
    const auto referencePath = dir.path / "reference.fa";
    const auto inputPath = dir.path / "input.fastq";
    const auto pafPath = dir.path / "input.paf";
    const auto archivePath = dir.path / "output.fqmr";

    writeFile(referencePath, ">chr1\n" + fixture.reference + "\n");
    writeFile(inputPath, fixture.fastq);
    writeFile(pafPath, fixture.paf);
    const auto compressed = compress({.inputPath = inputPath,
                                      .pafPath = pafPath,
                                      .referencePath = referencePath,
                                      .outputPath = archivePath,
                                      .blockBases = 1000,
                                      .maxMismatches = 8,
                                      .forceOverwrite = true});
    ASSERT_TRUE(compressed) << compressed.error().message;

    const auto size = std::filesystem::file_size(archivePath);
    ASSERT_GT(size, 1U);
    std::filesystem::resize_file(archivePath, size - 1);
    const auto verified = verify(archivePath, referencePath);
    ASSERT_FALSE(verified);
}

TEST(RefArchiveTest, CompressesMultipleBlocksAndHonorsMismatchLimit) {
    auto dir = makeTempDir();
    const auto fixture = makeFixture();
    const auto referencePath = dir.path / "reference.fa";
    const auto inputPath = dir.path / "input.fastq";
    const auto pafPath = dir.path / "input.paf";
    const auto archivePath = dir.path / "output.fqmr";
    const auto restoredPath = dir.path / "restored.fastq";

    writeFile(referencePath, ">chr1\n" + fixture.reference + "\n");
    writeFile(inputPath, fixture.fastq);
    writeFile(pafPath, fixture.paf);

    const auto compressed = compress({.inputPath = inputPath,
                                      .pafPath = pafPath,
                                      .referencePath = referencePath,
                                      .outputPath = archivePath,
                                      .blockBases = 20,
                                      .maxMismatches = 0,
                                      .forceOverwrite = true});
    ASSERT_TRUE(compressed) << compressed.error().message;
    EXPECT_EQ(compressed->blocks, 5U);
    EXPECT_EQ(compressed->mapped, 2U);
    EXPECT_EQ(compressed->literal, 3U);

    const auto verified = verify(archivePath, referencePath);
    ASSERT_TRUE(verified) << verified.error().message;
    EXPECT_EQ(verified->blocks, 5U);
    EXPECT_EQ(verified->mapped, 2U);
    EXPECT_EQ(verified->literal, 3U);

    const auto decompressed = decompress(archivePath, referencePath, restoredPath);
    ASSERT_TRUE(decompressed) << decompressed.error().message;
    EXPECT_EQ(readFile(restoredPath), fixture.fastq);
}

TEST(RefArchiveTest, SupportsEmptyArchive) {
    auto dir = makeTempDir();
    const auto referencePath = dir.path / "reference.fa";
    const auto inputPath = dir.path / "empty.fastq";
    const auto pafPath = dir.path / "empty.paf";
    const auto archivePath = dir.path / "empty.fqmr";
    const auto restoredPath = dir.path / "restored.fastq";

    writeFile(referencePath, ">chr1\n" + referenceSequence() + "\n");
    writeFile(inputPath, "");
    writeFile(pafPath, "");

    const auto compressed = compress({.inputPath = inputPath,
                                      .pafPath = pafPath,
                                      .referencePath = referencePath,
                                      .outputPath = archivePath,
                                      .blockBases = 1000,
                                      .maxMismatches = 8,
                                      .forceOverwrite = true});
    ASSERT_TRUE(compressed) << compressed.error().message;
    EXPECT_EQ(compressed->blocks, 0U);
    EXPECT_EQ(compressed->records, 0U);

    const auto verified = verify(archivePath, referencePath);
    ASSERT_TRUE(verified) << verified.error().message;
    EXPECT_EQ(verified->blocks, 0U);
    EXPECT_EQ(verified->records, 0U);

    const auto decompressed = decompress(archivePath, referencePath, restoredPath);
    ASSERT_TRUE(decompressed) << decompressed.error().message;
    EXPECT_EQ(readFile(restoredPath), "");
}

TEST(RefArchiveTest, RejectsTrailingBytesAfterFooter) {
    auto dir = makeTempDir();
    const auto fixture = makeFixture();
    const auto referencePath = dir.path / "reference.fa";
    const auto inputPath = dir.path / "input.fastq";
    const auto pafPath = dir.path / "input.paf";
    const auto archivePath = dir.path / "output.fqmr";

    writeFile(referencePath, ">chr1\n" + fixture.reference + "\n");
    writeFile(inputPath, fixture.fastq);
    writeFile(pafPath, fixture.paf);
    const auto compressed = compress({.inputPath = inputPath,
                                      .pafPath = pafPath,
                                      .referencePath = referencePath,
                                      .outputPath = archivePath,
                                      .blockBases = 1000,
                                      .maxMismatches = 8,
                                      .forceOverwrite = true});
    ASSERT_TRUE(compressed) << compressed.error().message;

    {
        std::ofstream output(archivePath, std::ios::binary | std::ios::app);
        output.put('x');
    }
    const auto verified = verify(archivePath, referencePath);
    ASSERT_FALSE(verified);
    EXPECT_EQ(verified.error().code, ErrorCode::kFormatError);
}

TEST(RefArchiveTest, UsesFrontCodingForStructuredIds) {
    auto dir = makeTempDir();
    const auto referencePath = dir.path / "reference.fa";
    const auto inputPath = dir.path / "input.fastq";
    const auto pafPath = dir.path / "input.paf";
    const auto archivePath = dir.path / "output.fqmr";
    const auto restoredPath = dir.path / "restored.fastq";

    const auto reference = referenceSequence();
    const std::string quality(20, 'I');
    std::string fastq;
    std::string paf;
    for (int i = 0; i < 2000; ++i) {
        const auto id = "A00123:1:H7YRLADXX:1:1101:" + std::to_string(1000 + i / 1000) + ":" +
            std::to_string(2000 + i % 1000);
        fastq += '@' + id + " 1:N:0:ATCACG\n" + reference.substr(0, 20) + "\n+\n" + quality + "\n";
        paf += id +
            "\t20\t0\t20\t+\tchr1\t200\t0\t20\t20\t20\t60\ttp:A:P\t"
            "NM:i:0\tcg:Z:20M\n";
    }

    writeFile(referencePath, ">chr1\n" + reference + "\n");
    writeFile(inputPath, fastq);
    writeFile(pafPath, paf);

    const auto compressed = compress({.inputPath = inputPath,
                                      .pafPath = pafPath,
                                      .referencePath = referencePath,
                                      .outputPath = archivePath,
                                      .blockBases = 1'000'000,
                                      .maxMismatches = 8,
                                      .forceOverwrite = true});
    ASSERT_TRUE(compressed) << compressed.error().message;
    EXPECT_EQ(compressed->blocks, 1U);
    EXPECT_EQ(compressed->records, 2000U);
    EXPECT_EQ(compressed->frontIdBlocks, 1U);

    const auto verified = verify(archivePath, referencePath);
    ASSERT_TRUE(verified) << verified.error().message;
    EXPECT_EQ(verified->frontIdBlocks, 1U);

    const auto decompressed = decompress(archivePath, referencePath, restoredPath);
    ASSERT_TRUE(decompressed) << decompressed.error().message;
    EXPECT_EQ(readFile(restoredPath), fastq);
}

TEST(RefArchiveTest, RoundTripsClippedAndIndelReads) {
    auto dir = makeTempDir();
    const auto reference = referenceSequence();
    const auto referencePath = dir.path / "reference.fa";
    const auto inputPath = dir.path / "input.fastq";
    const auto pafPath = dir.path / "input.paf";
    const auto archivePath = dir.path / "output.fqmr";
    const auto restoredPath = dir.path / "restored.fastq";
    const std::string quality(20, 'I');

    const auto reverse = [](std::string_view sequence) {
        std::string result;
        result.reserve(sequence.size());
        for (auto it = sequence.rbegin(); it != sequence.rend(); ++it) {
            switch (*it) {
                case 'A':
                    result.push_back('T');
                    break;
                case 'C':
                    result.push_back('G');
                    break;
                case 'G':
                    result.push_back('C');
                    break;
                default:
                    result.push_back('A');
                    break;
            }
        }
        return result;
    };

    std::string fastq;
    std::string paf;
    const auto add = [&](std::string_view id,
                         std::string_view sequence,
                         std::uint64_t queryStart,
                         std::uint64_t queryEnd,
                         char strand,
                         std::uint64_t targetStart,
                         std::uint64_t targetEnd,
                         std::string_view cigar) {
        fastq += '@';
        fastq += id;
        fastq += '\n';
        fastq += sequence;
        fastq += "\n+\n";
        fastq += quality;
        fastq += '\n';
        paf += id;
        paf += "\t20\t" + std::to_string(queryStart) + "\t" + std::to_string(queryEnd) + "\t";
        paf += strand;
        paf += "\tchr1\t200\t" + std::to_string(targetStart) + "\t" + std::to_string(targetEnd) +
            "\t20\t20\t60\ttp:A:P\tcg:Z:";
        paf += cigar;
        paf += '\n';
    };

    add("clip-right", reference.substr(0, 15) + "TTTTT", 0, 15, '+', 0, 15, "15M");
    add("clip-left", "GG" + reference.substr(2, 18), 2, 20, '+', 2, 20, "18M");
    add("insertion",
        reference.substr(40, 10) + "A" + reference.substr(50, 9),
        0,
        20,
        '+',
        40,
        59,
        "10M1I9M");
    add("deletion",
        reference.substr(60, 10) + reference.substr(71, 10),
        0,
        20,
        '+',
        60,
        81,
        "10M1D10M");
    add("reverse-insertion",
        reverse(reference.substr(100, 10) + "C" + reference.substr(110, 9)),
        0,
        20,
        '-',
        100,
        119,
        "10M1I9M");
    add("reverse-clip", "TT" + reverse(reference.substr(160, 18)), 2, 20, '-', 160, 178, "18M");

    writeFile(referencePath, ">chr1\n" + reference + "\n");
    writeFile(inputPath, fastq);
    writeFile(pafPath, paf);

    const auto compressed = compress({.inputPath = inputPath,
                                      .pafPath = pafPath,
                                      .referencePath = referencePath,
                                      .outputPath = archivePath,
                                      .blockBases = 1000,
                                      .maxMismatches = 8,
                                      .forceOverwrite = true});
    ASSERT_TRUE(compressed) << compressed.error().message;
    EXPECT_EQ(compressed->records, 6U);
    EXPECT_EQ(compressed->mapped, 6U);
    EXPECT_EQ(compressed->literal, 0U);
    EXPECT_EQ(compressed->clipped, 3U);
    EXPECT_EQ(compressed->indel, 3U);

    const auto verified = verify(archivePath, referencePath);
    ASSERT_TRUE(verified) << verified.error().message;
    EXPECT_EQ(verified->mapped, 6U);
    EXPECT_EQ(verified->clipped, 3U);
    EXPECT_EQ(verified->indel, 3U);

    const auto decompressed = decompress(archivePath, referencePath, restoredPath);
    ASSERT_TRUE(decompressed) << decompressed.error().message;
    EXPECT_EQ(readFile(restoredPath), fastq);
}

TEST(RefArchiveTest, RejectsOutputPathAliasingInputOrReference) {
    auto dir = makeTempDir();
    const auto fixture = makeFixture();
    const auto referencePath = dir.path / "reference.fa";
    const auto inputPath = dir.path / "input.fastq";
    const auto pafPath = dir.path / "input.paf";

    writeFile(referencePath, ">chr1\n" + fixture.reference + "\n");
    writeFile(inputPath, fixture.fastq);
    writeFile(pafPath, fixture.paf);

    const auto overwriteReference = compress({.inputPath = inputPath,
                                              .pafPath = pafPath,
                                              .referencePath = referencePath,
                                              .outputPath = referencePath,
                                              .blockBases = 1000,
                                              .maxMismatches = 8,
                                              .forceOverwrite = true});
    ASSERT_FALSE(overwriteReference);
    EXPECT_EQ(overwriteReference.error().code, ErrorCode::kUsageError);

    const auto overwriteInput = compress({.inputPath = inputPath,
                                          .pafPath = pafPath,
                                          .referencePath = referencePath,
                                          .outputPath = inputPath,
                                          .blockBases = 1000,
                                          .maxMismatches = 8,
                                          .forceOverwrite = true});
    ASSERT_FALSE(overwriteInput);
    EXPECT_EQ(overwriteInput.error().code, ErrorCode::kUsageError);
}

}  // namespace fqc::tools::ref::test
