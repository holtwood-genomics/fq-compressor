// =============================================================================
// fq-compressor - Experimental fqc-max/v1 Tests
// =============================================================================

#include "fqc/common/error.h"
#include "fqc/common/types.h"
#include "fqc/format/max_ratio.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace fqc::format::test {

namespace {

[[nodiscard]] auto makeRecords() -> std::vector<ReadRecord> {
    return {
        {"read_1", " comment", "ACGTN", "IIIII"},
        {"read_2", "acgtrysw", "!#$%&'()"},
        {"read_3", "AAAA", "JJJJ"},
        {"read_4", "TGCATGCA", "########"},
        {"read_5", "NNNN", "IIII"},
    };
}

[[nodiscard]] auto writeArchive(std::span<const ReadRecord> first,
                                std::span<const ReadRecord> second,
                                MaxArchiveOptions options = {}) -> std::string {
    std::ostringstream output(std::ios::binary);
    MaxArchiveWriter writer(output, options);
    EXPECT_TRUE(writer.writeBlock(first).has_value());
    if (!second.empty()) {
        EXPECT_TRUE(writer.writeBlock(second).has_value());
    }
    EXPECT_TRUE(writer.finish().has_value());
    return output.str();
}

[[nodiscard]] auto readAll(std::istream& input) -> Result<std::vector<ReadRecord>> {
    MaxArchiveReader reader(input);
    auto metadata = reader.open();
    if (!metadata) {
        return makeError<std::vector<ReadRecord>>(metadata.error());
    }
    std::vector<ReadRecord> records;
    for (;;) {
        auto block = reader.readBlock();
        if (!block) {
            return makeError<std::vector<ReadRecord>>(block.error());
        }
        if (!block->has_value()) {
            break;
        }
        for (auto& record : **block) {
            records.push_back(std::move(record));
        }
    }
    return records;
}

}  // namespace

TEST(MaxArchiveTest, RoundTripsRecordsAcrossBlocks) {
    const auto records = makeRecords();
    const auto archive = writeArchive(std::span(records).first(2), std::span(records).subspan(2));

    ASSERT_GE(archive.size(), 8U);
    EXPECT_EQ(archive.substr(0, 8), std::string("FQCMX1\r\n"));

    std::istringstream input(archive, std::ios::binary);
    MaxArchiveReader reader(input);
    const auto metadata = reader.open();
    ASSERT_TRUE(metadata.has_value()) << metadata.error().message;
    EXPECT_EQ(metadata->version, kMaxArchiveVersion);
    EXPECT_EQ(metadata->profile, DatasetProfile::kIllumina);

    std::vector<ReadRecord> decoded;
    for (;;) {
        auto block = reader.readBlock();
        ASSERT_TRUE(block.has_value()) << block.error().message;
        if (!block->has_value()) {
            break;
        }
        for (auto& record : **block) {
            decoded.push_back(std::move(record));
        }
    }
    EXPECT_EQ(decoded, records);
    EXPECT_EQ(reader.stats().blockCount, 2U);
    EXPECT_EQ(reader.stats().recordCount, records.size());
}

TEST(MaxArchiveTest, SupportsEmptyArchive) {
    std::ostringstream output(std::ios::binary);
    MaxArchiveWriter writer(output, {});
    ASSERT_TRUE(writer.finish().has_value());
    const auto archive = output.str();

    std::istringstream input(archive, std::ios::binary);
    MaxArchiveReader reader(input);
    auto metadata = reader.open();
    ASSERT_TRUE(metadata.has_value()) << metadata.error().message;
    auto block = reader.readBlock();
    ASSERT_TRUE(block.has_value()) << block.error().message;
    EXPECT_FALSE(block->has_value());
    EXPECT_EQ(reader.stats().blockCount, 0U);
}

TEST(MaxArchiveTest, RejectsTruncatedArchive) {
    const auto records = makeRecords();
    auto archive = writeArchive(records, {});
    archive.resize(archive.size() - 10);
    std::istringstream input(archive, std::ios::binary);
    const auto result = readAll(input);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::kFormatError);
}

TEST(MaxArchiveTest, RejectsCorruptBlockPayload) {
    const auto records = makeRecords();
    auto archive = writeArchive(records, {});
    ASSERT_GT(archive.size(), 130U);
    // Header 32 + block header 88 = payload starts at 120. Flip a byte inside
    // the ID payload; the block checksum must catch it if zstd does not.
    archive[125] = static_cast<char>(archive[125] ^ 0x5A);
    std::istringstream input(archive, std::ios::binary);
    const auto result = readAll(input);
    ASSERT_FALSE(result.has_value());
}

TEST(MaxArchiveTest, RejectsUnknownMagic) {
    std::string archive = "NOTFQC!!";
    std::istringstream input(archive, std::ios::binary);
    MaxArchiveReader reader(input);
    const auto result = reader.open();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::kFormatError);
}

}  // namespace fqc::format::test
