#!/usr/bin/env bash
# 复现 SRR14724532 R1 头部 + 完整 hg38 的参考归档验收。
# 数据默认落在 /tmp，不写入仓库；可用 FQC_REAL_WGS_DIR 覆盖。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${FQC_REAL_WGS_DIR:-/tmp/fqc-real-wgs-20261115}"
DATA="$OUT/data"
TOOLS="$OUT/tools"
THREADS="${FQC_REAL_WGS_THREADS:-12}"
FQ="$DATA/SRR14724532_R1.head.fastq"
GZ="$DATA/SRR14724532_1.head21m.fastq.gz"
PAF="$DATA/SRR14724532_R1.head.paf"
REF_GZ="$DATA/hg38.fa.gz"
REF="$DATA/hg38.fa"
MM="$TOOLS/minimap2-2.31_x64-linux/minimap2"
ARCHIVE="$DATA/SRR14724532_R1.head.fixed.fqmr"
RESTORED="$DATA/SRR14724532_R1.head.fixed.restored.fastq"
BIN="$ROOT/build/clang-debug/tools/fqc_ref_archive"
FQC="$ROOT/build/clang-debug/src/fqc"

mkdir -p "$DATA" "$TOOLS"

check_size() {
    local path="$1" expected="$2"
    local actual
    actual="$(stat -c '%s' "$path")"
    if [[ "$actual" != "$expected" ]]; then
        echo "size mismatch: $path: got $actual, expected $expected" >&2
        exit 1
    fi
}

check_sha() {
    local path="$1" expected="$2"
    local actual
    actual="$(sha256sum "$path" | cut -d' ' -f1)"
    if [[ "$actual" != "$expected" ]]; then
        echo "sha256 mismatch: $path: got $actual, expected $expected" >&2
        exit 1
    fi
}

# R1 gzip 头部；HTTP Range 截断 gzip，下面只保留完整 4 行记录。
if [[ ! -s "$GZ" ]]; then
    curl -L --fail --retry 3 --retry-delay 2 -r 0-21110783 -o "$GZ" \
        https://ftp.sra.ebi.ac.uk/vol1/fastq/SRR147/032/SRR14724532/SRR14724532_1.fastq.gz
fi
check_size "$GZ" 21110784
check_sha "$GZ" 286836d9d79ad8470ddcd2b395562aa4f25a347e8b424a9bb1ad5c159378b95b

if [[ ! -s "$FQ" ]]; then
    # 截断 gzip 的 EOF 不是数据错误；head 拿到完整记录后关闭管道。
    set +o pipefail
    gzip -dc "$GZ" 2>/dev/null | head -n 1195896 > "$FQ"
    set -o pipefail
fi
check_size "$FQ" 99934080
check_sha "$FQ" d4f6c8bee191ee8cc5207cca5fb86659a2aaa02e3f493b35f2ed50027ab6e0f4

if [[ ! -s "$REF_GZ" ]]; then
    curl -L --fail --retry 3 --retry-delay 2 -o "$REF_GZ" \
        https://hgdownload.soe.ucsc.edu/goldenPath/hg38/bigZips/hg38.fa.gz
fi
check_size "$REF_GZ" 983659424
check_sha "$REF_GZ" c1dd87068c254eb53d944f71e51d1311964fce8de24d6fc0effc9c61c01527d4
if [[ ! -s "$REF" ]]; then
    gzip -dc "$REF_GZ" > "$REF"
fi
check_size "$REF" 3273481150
check_sha "$REF" 5be01555d98347fdb3714dc84c6f77c9d8bc774adcf32c6f7a8fa06f5baf5e51

if [[ ! -x "$MM" ]]; then
    MM_TAR="$TOOLS/minimap2-2.31_x64-linux.tar.bz2"
    curl -L --fail --retry 3 --retry-delay 2 -o "$MM_TAR" \
        https://github.com/lh3/minimap2/releases/download/v2.31/minimap2-2.31_x64-linux.tar.bz2
    tar -xjf "$MM_TAR" -C "$TOOLS"
fi

if [[ ! -s "$DATA/hg38.mmi" ]]; then
    "$MM" -d "$DATA/hg38.mmi" "$REF" -t "$THREADS"
fi
if [[ ! -s "$PAF" ]]; then
    "$MM" -x sr -c --secondary=no -t "$THREADS" "$DATA/hg38.mmi" "$FQ" > "$PAF.tmp"
    mv "$PAF.tmp" "$PAF"
fi
check_size "$PAF" 49043976
check_sha "$PAF" c33900ac67b0e0d5bad2297767477ecef5408e6cea0313318c44866c4983c935

if [[ ! -x "$BIN" ]]; then
    echo "missing $BIN; run ./scripts/build.sh clang-debug first" >&2
    exit 1
fi

if [[ ! -s "$ARCHIVE" ]]; then
    "$BIN" compress -i "$FQ" --paf "$PAF" -r "$REF" -o "$ARCHIVE" \
        --block-bases 4000000 --max-mismatches 16 -f
fi
check_size "$ARCHIVE" 4547935
check_sha "$ARCHIVE" 6f50a3146e018c33fffe6be3a21af9163b8ae1223d1b386f6f440507bb3b647a

"$BIN" verify -i "$ARCHIVE" -r "$REF"
"$BIN" decompress -i "$ARCHIVE" -r "$REF" -o "$RESTORED" -f
cmp "$FQ" "$RESTORED"
check_sha "$RESTORED" d4f6c8bee191ee8cc5207cca5fb86659a2aaa02e3f493b35f2ed50027ab6e0f4

if [[ ! -s "$DATA/reads.gzip6.gz" ]]; then
    gzip -6 -n -c "$FQ" > "$DATA/reads.gzip6.gz"
fi
if [[ ! -s "$DATA/reads.zstd19.zst" ]]; then
    zstd -19 -q -f -o "$DATA/reads.zstd19.zst" "$FQ"
fi
if [[ ! -s "$DATA/SRR14724532_R1.head.fqc" ]]; then
    "$FQC" compress -i "$FQ" -o "$DATA/SRR14724532_R1.head.fqc" --force
fi

if command -v samtools >/dev/null 2>&1; then
    if [[ ! -s "$REF.fai" ]]; then
        samtools faidx "$REF"
    fi
    SAM="$DATA/SRR14724532_R1.head.sam"
    BAM="$DATA/SRR14724532_R1.head.bam"
    CRAM="$DATA/SRR14724532_R1.head.cram"
    if [[ ! -s "$SAM" ]]; then
        "$MM" -a -x sr --secondary=no -t "$THREADS" "$DATA/hg38.mmi" "$FQ" > "$SAM"
    fi
    if [[ ! -s "$BAM" ]]; then
        samtools sort -@ 4 -m 1G -o "$BAM" "$SAM"
    fi
    if [[ ! -s "$CRAM" ]]; then
        samtools view -C -T "$REF" -@ 4 -o "$CRAM" "$BAM"
    fi
    samtools index "$CRAM"
    samtools quickcheck "$CRAM"
    [[ "$(samtools view -c -F 0x900 "$CRAM")" == "298974" ]]
fi

printf '\n=== real WGS reference benchmark ===\n'
sha256sum "$FQ" "$PAF" "$REF" "$ARCHIVE" "$RESTORED"
stat -c '%s %n' "$FQ" "$PAF" "$REF" "$ARCHIVE" \
    "$DATA/reads.gzip6.gz" "$DATA/reads.zstd19.zst" \
    "$DATA/SRR14724532_R1.head.fqc" "$DATA/SRR14724532_R1.head.cram" 2>/dev/null || true
