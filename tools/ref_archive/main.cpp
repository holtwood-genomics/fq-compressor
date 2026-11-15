// =============================================================================
// fq-compressor - Reference-Backed Archive CLI
// =============================================================================

#include "fqc/common/error.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>

#include "ref_archive.h"

#include <CLI/CLI.hpp>

namespace {

void report(const fqc::Result<fqc::tools::ref::RefStats>& result, bool compressing) {
    if (!result) {
        std::cerr << fqc::errorCodeToString(result.error().code) << ": " << result.error().message
                  << '\n';
        std::exit(fqc::toExitCode(result.error().code));
    }
    const auto& stats = *result;
    std::cout << "blocks=" << stats.blocks << " records=" << stats.records
              << " pairs=" << stats.pairs << " bases=" << stats.bases << " mapped=" << stats.mapped
              << " literal=" << stats.literal << " clipped=" << stats.clipped
              << " indel=" << stats.indel << " ids=" << stats.idsBytes
              << " qualities=" << stats.qualitiesBytes << " alignments=" << stats.alignmentsBytes
              << " fqz_blocks=" << stats.fqzBlocks
              << " zstd_quality_blocks=" << stats.zstdQualityBlocks
              << " front_id_blocks=" << stats.frontIdBlocks << " mate_deltas=" << stats.mateDeltas
              << " input=" << stats.inputBytes << " output=" << stats.outputBytes;
    if (stats.outputBytes != 0 && stats.inputBytes != 0) {
        const auto numerator = compressing ? stats.inputBytes : stats.outputBytes;
        const auto denominator = compressing ? stats.outputBytes : stats.inputBytes;
        std::cout << (compressing ? " raw/archive=" : " restored/archive=") << std::fixed
                  << std::setprecision(3)
                  << static_cast<double>(numerator) / static_cast<double>(denominator) << 'x';
    }
    std::cout << '\n';
}

}  // namespace

auto main(int argc, char* argv[]) -> int {
    CLI::App app{"fqc-max-ref/v1: PAF-sidecar reference prototype"};
    app.require_subcommand(1);

    std::string input;
    std::string paf;
    std::string mateInput;
    std::string matePaf;
    std::string reference;
    std::string output;
    std::string r1Output;
    std::string r2Output;
    std::uint64_t blockBases = 4'000'000;
    std::uint32_t maxMismatches = 16;
    bool force = false;

    auto* compressCommand =
        app.add_subcommand("compress", "Compress FASTQ with a PAF sidecar and reference");
    compressCommand->add_option("-i,--input", input, "Input FASTQ")->required();
    compressCommand->add_option("--paf", paf, "PAF alignment sidecar")->required();
    compressCommand->add_option("--mate", mateInput, "Mate FASTQ for paired compression");
    compressCommand->add_option("--mate-paf", matePaf, "Mate PAF alignment sidecar");
    compressCommand->add_option("-r,--reference", reference, "External reference FASTA")
        ->required();
    compressCommand->add_option("-o,--output", output, "Output fqc-max-ref/v1 archive")->required();
    compressCommand->add_option("--block-bases", blockBases, "Target bases per block")
        ->default_val(4'000'000)
        ->check(CLI::PositiveNumber);
    compressCommand->add_option("--max-mismatches", maxMismatches, "Mapped-read mismatch limit")
        ->default_val(16)
        ->check(CLI::Range(0, 10'000));
    compressCommand->add_flag("-f,--force", force, "Overwrite an existing output");

    auto* decompressCommand =
        app.add_subcommand("decompress", "Decompress an fqc-max-ref/v1 archive");
    decompressCommand->add_option("-i,--input", input, "Input archive")->required();
    decompressCommand->add_option("-r,--reference", reference, "External reference FASTA")
        ->required();
    decompressCommand->add_option("-o,--output", output, "Output FASTQ")->required();
    decompressCommand->add_flag("-f,--force", force, "Overwrite an existing output");

    auto* decompressPairedCommand =
        app.add_subcommand("decompress-paired", "Decompress a paired archive into R1/R2 FASTQ");
    decompressPairedCommand->add_option("-i,--input", input, "Input archive")->required();
    decompressPairedCommand->add_option("-r,--reference", reference, "External reference FASTA")
        ->required();
    decompressPairedCommand->add_option("--r1", r1Output, "Output R1 FASTQ")->required();
    decompressPairedCommand->add_option("--r2", r2Output, "Output R2 FASTQ")->required();
    decompressPairedCommand->add_flag("-f,--force", force, "Overwrite existing outputs");

    auto* verifyCommand = app.add_subcommand("verify", "Verify an fqc-max-ref/v1 archive");
    verifyCommand->add_option("-i,--input", input, "Input archive")->required();
    verifyCommand->add_option("-r,--reference", reference, "External reference FASTA")->required();

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        return app.exit(error);
    }

    if (*compressCommand) {
        report(fqc::tools::ref::compress({.inputPath = input,
                                          .pafPath = paf,
                                          .referencePath = reference,
                                          .outputPath = output,
                                          .blockBases = blockBases,
                                          .maxMismatches = maxMismatches,
                                          .forceOverwrite = force,
                                          .mateInputPath = mateInput.empty()
                                              ? std::nullopt
                                              : std::optional<std::filesystem::path>(mateInput),
                                          .matePafPath = matePaf.empty()
                                              ? std::nullopt
                                              : std::optional<std::filesystem::path>(matePaf)}),
               true);
    } else if (*decompressCommand) {
        report(fqc::tools::ref::decompress(input, reference, output, force), false);
    } else if (*decompressPairedCommand) {
        report(fqc::tools::ref::decompressPaired(input, reference, r1Output, r2Output, force),
               false);
    } else if (*verifyCommand) {
        report(fqc::tools::ref::verify(input, reference), false);
    }
    return 0;
}
