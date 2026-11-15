// =============================================================================
// fq-compressor - Phase 0 Ratio Probe CLI
// =============================================================================

#include "fqc/common/error.h"
#include "fqc/log.h"

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>

#include "ratio_probe.h"

#include <CLI/CLI.hpp>

namespace {

[[nodiscard]] auto reportError(const fqc::Error& error) -> int {
    FQC_LOG_ERROR("{}", error.message);
    return fqc::toExitCode(error.code);
}

}  // namespace

auto main(int argc, char* argv[]) -> int {
    try {
        std::string input;
        bool json = false;
        int zstdLevel = 19;
        int gzipLevel = 6;

        CLI::App app{"FQC Phase 0 ratio probe (development-only)"};
        app.add_option("-i,--input", input, "FASTQ input, .gz input, or '-' for stdin")->required();
        app.add_flag("--json", json, "Emit stable JSON instead of text");
        app.add_option("--zstd-level", zstdLevel, "zstd baseline level")
            ->default_val(19)
            ->check(CLI::Range(1, 22));
        app.add_option("--gzip-level", gzipLevel, "gzip baseline level")
            ->default_val(6)
            ->check(CLI::Range(0, 9));

        try {
            app.parse(argc, argv);
        } catch (const CLI::ParseError& error) {
            const auto cliExitCode = app.exit(error);
            return cliExitCode == 0 ? 0 : fqc::toExitCode(fqc::ErrorCode::kUsageError);
        }

        const auto stats =
            fqc::tools::analyzeFile(input, {.zstdLevel = zstdLevel, .gzipLevel = gzipLevel});
        if (!stats) {
            return reportError(stats.error());
        }

        std::cout << (json ? fqc::tools::toJson(*stats) : fqc::tools::toText(*stats));
        return 0;
    } catch (const std::exception& error) {
        FQC_LOG_ERROR("unexpected error: {}", error.what());
        return fqc::toExitCode(fqc::ErrorCode::kInternalError);
    }
}
