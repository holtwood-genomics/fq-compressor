// =============================================================================
// fq-compressor - Phase 0 Ratio Probe Tests
// =============================================================================

#include "ratio_probe.h"

#include "fqc/common/error.h"

#include <sstream>
#include <string>
#include <string_view>

#include "support.h"
#include <zlib.h>

#include <gtest/gtest.h>

namespace fqc::tools::test {

namespace {

[[nodiscard]] auto analyze(std::string_view fastq) -> RatioProbeStats {
    std::istringstream input{std::string(fastq)};
    RatioProbeStats stats;
    const auto result = analyzeFastq(input, {}, stats);
    EXPECT_TRUE(result.has_value()) << result.error().message;
    return stats;
}

}  // namespace

TEST(RatioProbeTest, AnalyzesCanonicalFastqAndStreams) {
    const std::string fastq =
        "@r1 comment\n"
        "ACGTN\n"
        "+\n"
        "IIIII\n"
        "@r2\n"
        "acgt\n"
        "+r2\n"
        "!!!!\n";
    const auto stats = analyze(fastq);

    EXPECT_EQ(stats.totalBytes, fastq.size());
    EXPECT_EQ(stats.records, 2U);
    EXPECT_EQ(stats.bases, 9U);
    EXPECT_EQ(stats.acgtBases, 8U);
    EXPECT_EQ(stats.nBases, 1U);
    EXPECT_EQ(stats.otherBases, 0U);
    EXPECT_EQ(stats.lines, 8U);
    EXPECT_EQ(stats.blankLines, 0U);
    EXPECT_EQ(stats.lfLineEndings, 8U);
    EXPECT_EQ(stats.crlfLineEndings, 0U);
    EXPECT_EQ(stats.finalLineWithoutNewline, 0U);

    EXPECT_EQ(stats.header.content, 14U);
    EXPECT_EQ(stats.header.lineEndings, 2U);
    EXPECT_EQ(stats.sequence.content, 9U);
    EXPECT_EQ(stats.sequence.lineEndings, 2U);
    EXPECT_EQ(stats.plus.content, 4U);
    EXPECT_EQ(stats.plus.lineEndings, 2U);
    EXPECT_EQ(stats.quality.content, 9U);
    EXPECT_EQ(stats.quality.lineEndings, 2U);
    EXPECT_EQ(stats.blank.total(), 0U);

    EXPECT_EQ(stats.idBytes, 4U);
    EXPECT_EQ(stats.commentBytes, 8U);
    EXPECT_EQ(stats.plusEmpty, 1U);
    EXPECT_EQ(stats.plusMatchesHeader, 1U);
    EXPECT_EQ(stats.plusLiteral, 0U);

    EXPECT_EQ(stats.qualitySymbols(), 2U);
    EXPECT_EQ(stats.qualityHistogram[static_cast<unsigned char>('I')], 5U);
    EXPECT_EQ(stats.qualityHistogram[static_cast<unsigned char>('!')], 4U);
    EXPECT_DOUBLE_EQ(stats.meanQuality(), 200.0 / 9.0);

    // FQC-like packed payload: record-count varint + per-read length/packed
    // bytes + exception count/delta/char. Lowercase acgt are exceptions in the
    // production encoder, while ACGTN has one N exception.
    EXPECT_EQ(stats.packedMainBytes, 5U);
    EXPECT_EQ(stats.packedExceptionBytes, 12U);
    EXPECT_EQ(stats.packedSequenceBytes, 18U);

    EXPECT_GT(stats.gzipBytes, 0U);
    EXPECT_GT(stats.zstdBytes, 0U);
    EXPECT_GT(stats.idZstdBytes, 0U);
    EXPECT_GT(stats.qualityZstdBytes, 0U);
}

TEST(RatioProbeTest, PreservesCrlfEmptyReadAndMissingFinalNewline) {
    const std::string fastq =
        "@r1\r\n"
        "\r\n"
        "+\r\n"
        "\r\n"
        "@r2\n"
        "ACGT\n"
        "+\n"
        "IIII";
    const auto stats = analyze(fastq);

    EXPECT_EQ(stats.totalBytes, fastq.size());
    EXPECT_EQ(stats.records, 2U);
    EXPECT_EQ(stats.bases, 4U);
    EXPECT_EQ(stats.emptySequences, 1U);
    EXPECT_EQ(stats.lines, 8U);
    EXPECT_EQ(stats.crlfLineEndings, 4U);
    EXPECT_EQ(stats.lfLineEndings, 3U);
    EXPECT_EQ(stats.finalLineWithoutNewline, 1U);
    EXPECT_EQ(stats.plusEmpty, 2U);
    EXPECT_EQ(stats.qualitySymbols(), 1U);
    EXPECT_DOUBLE_EQ(stats.meanQuality(), 40.0);
    EXPECT_EQ(stats.packedSequenceBytes, 6U);
}

TEST(RatioProbeTest, CountsBlankLinesWithoutTreatingThemAsRecords) {
    const std::string fastq =
        "\n"
        "\n"
        "@r1\n"
        "A\n"
        "+\n"
        "I\n";
    const auto stats = analyze(fastq);

    EXPECT_EQ(stats.blankLines, 2U);
    EXPECT_EQ(stats.records, 1U);
    EXPECT_EQ(stats.blank.lineEndings, 2U);
    EXPECT_EQ(stats.totalBytes, fastq.size());
}

TEST(RatioProbeTest, ClassifiesLiteralPlusLine) {
    const std::string fastq = "@r1\nACGT\n+other\nIIII\n";
    const auto stats = analyze(fastq);

    EXPECT_EQ(stats.plusLiteral, 1U);
    EXPECT_EQ(stats.plusEmpty, 0U);
    EXPECT_EQ(stats.plusMatchesHeader, 0U);
}

TEST(RatioProbeTest, RejectsMalformedInput) {
    const std::string cases[] = {
        "r1\nACGT\n+\nIIII\n",          // missing '@'
        "@r1\nACGT\nnot-plus\nIIII\n",  // missing '+'
        "@r1\nACGT\n+\nIII\n",          // quality length mismatch
        "@r1\nACGT\n+\n",               // truncated quality line
        "@\nACGT\n+\nIIII\n",           // empty ID
    };

    for (const auto& fastq : cases) {
        std::istringstream input{fastq};
        RatioProbeStats stats;
        const auto result = analyzeFastq(input, {}, stats);
        ASSERT_FALSE(result.has_value()) << fastq;
        EXPECT_EQ(result.error().code, ErrorCode::kFormatError) << fastq;
    }
}

TEST(RatioProbeTest, RejectsInvalidCompressionLevels) {
    std::istringstream input{"@r1\nA\n+\nI\n"};
    RatioProbeStats stats;
    const auto zstdResult = analyzeFastq(input, {.zstdLevel = 0, .gzipLevel = 6}, stats);
    ASSERT_FALSE(zstdResult.has_value());
    EXPECT_EQ(zstdResult.error().code, ErrorCode::kUsageError);

    std::istringstream secondInput{"@r1\nA\n+\nI\n"};
    RatioProbeStats secondStats;
    const auto gzipResult =
        analyzeFastq(secondInput, {.zstdLevel = 19, .gzipLevel = 10}, secondStats);
    ASSERT_FALSE(gzipResult.has_value());
    EXPECT_EQ(gzipResult.error().code, ErrorCode::kUsageError);
}

TEST(RatioProbeTest, BaselinesAreDeterministic) {
    const std::string fastq =
        "@r1 comment\n"
        "ACGTN\n"
        "+\n"
        "IIIII\n"
        "@r2\n"
        "TGCA\n"
        "+r2\n"
        "JJJJ\n";
    const auto first = analyze(fastq);
    const auto second = analyze(fastq);

    EXPECT_EQ(first.gzipBytes, second.gzipBytes);
    EXPECT_EQ(first.zstdBytes, second.zstdBytes);
    EXPECT_EQ(first.idZstdBytes, second.idZstdBytes);
    EXPECT_EQ(first.qualityZstdBytes, second.qualityZstdBytes);
}

TEST(RatioProbeTest, AnalyzesPlainFileAndFillsInputMetadata) {
    fqc::test::TempDir dir;
    const auto path = dir.path() / "sample.fastq";
    const std::string fastq = "@r1\nACGT\n+\nIIII\n";
    dir.writeFile("sample.fastq", fastq);

    const auto result = analyzeFile(path, {});
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->records, 1U);
    EXPECT_EQ(result->inputPath, path.string());
    EXPECT_EQ(result->inputCompression, "none");
    EXPECT_TRUE(result->inputFileBytesKnown);
    EXPECT_EQ(result->inputFileBytes, fastq.size());
}

TEST(RatioProbeTest, AnalyzesGzipFile) {
    fqc::test::TempDir dir;
    const auto path = dir.path() / "sample.fastq.gz";
    const std::string fastq = "@r1\nACGT\n+\nIIII\n";

    gzFile file = gzopen(path.string().c_str(), "wb");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(gzwrite(file, fastq.data(), static_cast<unsigned>(fastq.size())),
              static_cast<int>(fastq.size()));
    ASSERT_EQ(gzclose(file), Z_OK);

    const auto result = analyzeFile(path, {});
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->records, 1U);
    EXPECT_EQ(result->inputCompression, "gzip");
    EXPECT_TRUE(result->inputFileBytesKnown);
    EXPECT_GT(result->inputFileBytes, 0U);
    EXPECT_EQ(result->totalBytes, fastq.size());
}

TEST(RatioProbeTest, JsonAndTextContainStableFields) {
    const auto stats = analyze("@r1\nACGT\n+\nIIII\n");
    const auto json = toJson(stats);
    EXPECT_NE(json.find("\"schema\": \"fqc-ratio-probe/v1\""), std::string::npos);
    EXPECT_NE(json.find("\"records\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"raw_streams\""), std::string::npos);
    EXPECT_NE(json.find("\"baselines\""), std::string::npos);
    EXPECT_NE(json.find("\"ratios\""), std::string::npos);

    const auto text = toText(stats);
    EXPECT_NE(text.find("records=1"), std::string::npos);
    EXPECT_NE(text.find("baselines:"), std::string::npos);
}

}  // namespace fqc::tools::test
