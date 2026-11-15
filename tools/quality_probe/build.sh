#!/usr/bin/env bash
# Build the development-only quality codec probe against an htscodecs source
# tree. It is intentionally outside the product CMake targets.
#
# Usage: ./tools/quality_probe/build.sh /path/to/htscodecs-1.6.7

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 /path/to/htscodecs-source" >&2
    exit 2
fi

HTSCODECS_DIR="$(cd "$1" && pwd)"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$PROJECT_DIR/build/quality-probe"
mkdir -p "$OUT_DIR"

if [[ ! -f "$HTSCODECS_DIR/htscodecs/fqzcomp_qual.c" ]]; then
    echo "error: $HTSCODECS_DIR does not look like an htscodecs source tree" >&2
    exit 1
fi

ZSTD_PKG=""
while IFS= read -r header; do
    root="${header%/include/zstd.h}"
    if [[ -f "$root/lib/libzstd.a" ]]; then
        ZSTD_PKG="$root"
        break
    fi
done < <(find "${CONAN_HOME:-$HOME/.conan2}/p" -path '*/p/include/zstd.h' 2>/dev/null)
if [[ -z "$ZSTD_PKG" ]]; then
    echo "error: zstd/1.5.7 package is not available in the Conan cache; run conan install ." >&2
    exit 1
fi

CC="${CC:-cc}"
CXX="${CXX:-c++}"
printf '#define HAVE_BUILTIN_PREFETCH 1\n' > "$OUT_DIR/config.h"

common_cflags=(-O2 -I"$OUT_DIR" -I"$HTSCODECS_DIR/htscodecs")
"$CC" "${common_cflags[@]}" -c "$HTSCODECS_DIR/htscodecs/fqzcomp_qual.c" \
    -o "$OUT_DIR/fqzcomp_qual.o"
"$CC" "${common_cflags[@]}" -c "$HTSCODECS_DIR/htscodecs/utils.c" \
    -o "$OUT_DIR/utils.o"

"$CXX" -std=c++23 -O2 -Wall -Wextra \
    -I"$HTSCODECS_DIR/htscodecs" -I"$ZSTD_PKG/include" \
    "$PROJECT_DIR/tools/quality_probe/quality_probe.cpp" \
    "$OUT_DIR/fqzcomp_qual.o" "$OUT_DIR/utils.o" \
    "$ZSTD_PKG/lib/libzstd.a" -lm -lpthread \
    -o "$OUT_DIR/fqc_quality_probe"

echo "built $OUT_DIR/fqc_quality_probe"
