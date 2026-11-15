#!/usr/bin/env bash
# 复现 SRR14724532 R1 头部 + 完整 hg38 的参考归档验收。
# FQC_BENCH_PAIRED=1 时追加 R2 头部、R2 单端归档与 paired 归档；
# FQC_BENCH_REPRO=1 时额外重压 paired 归档并做逐字节 cmp。
# 数据默认落在 /tmp，不写入仓库；可用 FQC_REAL_WGS_DIR 覆盖。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${FQC_REAL_WGS_DIR:-/tmp/fqc-real-wgs-20261115}"
DATA="$OUT/data"
TOOLS="$OUT/tools"
THREADS="${FQC_REAL_WGS_THREADS:-12}"
PAIRED="${FQC_BENCH_PAIRED:-0}"
REPRO="${FQC_BENCH_REPRO:-0}"
FQ="$DATA/SRR14724532_R1.head.fastq"
GZ="$DATA/SRR14724532_1.head21m.fastq.gz"
PAF="$DATA/SRR14724532_R1.head.paf"
R2_GZ="$DATA/SRR14724532_2.head40m.fastq.gz"
R2="$DATA/SRR14724532_R2.head.fastq"
R2_PAF="$DATA/SRR14724532_R2.head.paf"
REF_GZ="$DATA/hg38.fa.gz"
REF="$DATA/hg38.fa"
MM="$TOOLS/minimap2-2.31_x64-linux/minimap2"
ARCHIVE="$DATA/SRR14724532_R1.head.fixed.fqmr"
RESTORED="$DATA/SRR14724532_R1.head.fixed.restored.fastq"
R2_ARCHIVE="$DATA/SRR14724532_R2.head.fqmr"
PAIRED_ARCHIVE="$DATA/SRR14724532.paired.head.fqmr"
PAIRED_R1_RESTORED="$DATA/SRR14724532.paired.restored.R1.fastq"
PAIRED_R2_RESTORED="$DATA/SRR14724532.paired.restored.R2.fastq"
BIN="$ROOT/build/clang-debug/tools/fqc_ref_archive"
FQC="$ROOT/build/clang-debug/src/fqc"

mkdir -p "$DATA" "$TOOLS"

check_size() {
    local path="$1" expected="$2"
    local actual
    actual="$(stat -Lc '%s' "$path")"
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

run_external_baselines() {
    local fastq="$1" gzip_path="$2" zstd_path="$3" fqc_path="$4"
    if [[ ! -s "$gzip_path" ]]; then
        gzip -6 -n -c "$fastq" > "$gzip_path"
    fi
    if [[ ! -s "$zstd_path" ]]; then
        zstd -19 -q -f -o "$zstd_path" "$fastq"
    fi
    if [[ ! -s "$fqc_path" ]]; then
        "$FQC" compress -i "$fastq" -o "$fqc_path" --force
    fi
}

run_cram() {
    local fastq="$1" stem="$2"
    local sam="$DATA/$stem.sam"
    local bam="$DATA/$stem.bam"
    local cram="$DATA/$stem.cram"
    if [[ ! -s "$sam" ]]; then
        "$MM" -a -x sr --secondary=no -t "$THREADS" "$DATA/hg38.mmi" "$fastq" > "$sam"
    fi
    if [[ ! -s "$bam" ]]; then
        samtools sort -@ 4 -m 1G -o "$bam" "$sam"
    fi
    if [[ ! -s "$cram" ]]; then
        samtools view -C -T "$REF" -@ 4 -o "$cram" "$bam"
    fi
    samtools index "$cram"
    samtools quickcheck "$cram"
    [[ "$(samtools view -c -F 0x900 "$cram")" == "298974" ]]
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

if [[ "$PAIRED" == "1" ]]; then
    if [[ ! -s "$R2_GZ" ]]; then
        curl -L --fail --retry 3 --retry-delay 2 -r 0-39999999 -o "$R2_GZ" \
            https://ftp.sra.ebi.ac.uk/vol1/fastq/SRR147/032/SRR14724532/SRR14724532_2.fastq.gz
    fi
    check_size "$R2_GZ" 40000000
    check_sha "$R2_GZ" 53644acf28e7cfc132f13beb7fde816ef26b8cc1b66480211b48e8b878cf32ee
    if [[ ! -s "$R2" ]]; then
        set +o pipefail
        gzip -dc "$R2_GZ" 2>/dev/null | head -n 1195896 > "$R2"
        set -o pipefail
    fi
    check_size "$R2" 99934080
    check_sha "$R2" 546a6986830155f700dcead34e9cbff0291596a4e7887d760a3cd26467ee72e3
fi

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

if [[ "$PAIRED" == "1" ]]; then
    if [[ ! -s "$R2_PAF" ]]; then
        "$MM" -x sr -c --secondary=no -t "$THREADS" "$DATA/hg38.mmi" "$R2" > "$R2_PAF.tmp"
        mv "$R2_PAF.tmp" "$R2_PAF"
    fi
    check_size "$R2_PAF" 48788417
    check_sha "$R2_PAF" 46774f8584f240677420a2c86b0aa69d1e14fe658abeccf8b4fa2ba09d68c784
fi

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

run_external_baselines "$FQ" "$DATA/reads.gzip6.gz" "$DATA/reads.zstd19.zst" \
    "$DATA/SRR14724532_R1.head.fqc"

if [[ "$PAIRED" == "1" ]]; then
    if [[ ! -s "$R2_ARCHIVE" ]]; then
        "$BIN" compress -i "$R2" --paf "$R2_PAF" -r "$REF" -o "$R2_ARCHIVE" \
            --block-bases 4000000 --max-mismatches 16 -f
    fi
    check_size "$R2_ARCHIVE" 5130877
    check_sha "$R2_ARCHIVE" 2528b4cca24afb4298eec40c92d5163f94608c88ae746058199c95dd9ba9fb7f

    "$BIN" verify -i "$R2_ARCHIVE" -r "$REF"
    if [[ ! -s "$PAIRED_ARCHIVE" ]]; then
        "$BIN" compress -i "$FQ" --paf "$PAF" \
            --mate "$R2" --mate-paf "$R2_PAF" -r "$REF" -o "$PAIRED_ARCHIVE" \
            --block-bases 4000000 --max-mismatches 16 -f
    fi
    check_size "$PAIRED_ARCHIVE" 8825785
    check_sha "$PAIRED_ARCHIVE" 09cfec68802291c6f4342e3ddee338ed5d3dc5e6b404755895b00e28a58dd4e5

    "$BIN" verify -i "$PAIRED_ARCHIVE" -r "$REF"
    "$BIN" decompress-paired -i "$PAIRED_ARCHIVE" -r "$REF" \
        --r1 "$PAIRED_R1_RESTORED" --r2 "$PAIRED_R2_RESTORED" -f
    cmp "$FQ" "$PAIRED_R1_RESTORED"
    cmp "$R2" "$PAIRED_R2_RESTORED"
    check_sha "$PAIRED_R1_RESTORED" d4f6c8bee191ee8cc5207cca5fb86659a2aaa02e3f493b35f2ed50027ab6e0f4
    check_sha "$PAIRED_R2_RESTORED" 546a6986830155f700dcead34e9cbff0291596a4e7887d760a3cd26467ee72e3

    if [[ "$REPRO" == "1" ]]; then
        REPRO_ARCHIVE="$DATA/SRR14724532.paired.head.repro.fqmr"
        "$BIN" compress -i "$FQ" --paf "$PAF" \
            --mate "$R2" --mate-paf "$R2_PAF" -r "$REF" -o "$REPRO_ARCHIVE" \
            --block-bases 4000000 --max-mismatches 16 -f
        cmp "$PAIRED_ARCHIVE" "$REPRO_ARCHIVE"
    fi

    run_external_baselines "$R2" "$DATA/r2.gzip6.gz" "$DATA/r2.zstd19.zst" \
        "$DATA/SRR14724532_R2.head.fqc"
fi

if command -v samtools >/dev/null 2>&1; then
    if [[ ! -s "$REF.fai" ]]; then
        samtools faidx "$REF"
    fi
    run_cram "$FQ" "SRR14724532_R1.head"
    if [[ "$PAIRED" == "1" ]]; then
        run_cram "$R2" "SRR14724532_R2.head"
    fi
fi

printf '\n=== real WGS reference benchmark ===\n'
sha256sum "$FQ" "$PAF" "$REF" "$ARCHIVE" "$RESTORED"
stat -Lc '%s %n' "$FQ" "$PAF" "$REF" "$ARCHIVE" \
    "$DATA/reads.gzip6.gz" "$DATA/reads.zstd19.zst" \
    "$DATA/SRR14724532_R1.head.fqc" "$DATA/SRR14724532_R1.head.cram" 2>/dev/null || true
if [[ "$PAIRED" == "1" ]]; then
    printf '\n=== paired benchmark ===\n'
    sha256sum "$R2" "$R2_PAF" "$R2_ARCHIVE" "$PAIRED_ARCHIVE" \
        "$PAIRED_R1_RESTORED" "$PAIRED_R2_RESTORED"
    stat -Lc '%s %n' "$R2" "$R2_PAF" "$R2_ARCHIVE" "$PAIRED_ARCHIVE" \
        "$DATA/r2.gzip6.gz" "$DATA/r2.zstd19.zst" \
        "$DATA/SRR14724532_R2.head.fqc" "$DATA/SRR14724532_R2.head.cram" 2>/dev/null || true
fi
