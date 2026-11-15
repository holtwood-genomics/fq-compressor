# 2026-11-15 真实 WGS paired-end 参考归档：SRR14724532 R1+R2 头部

- 状态：`fqc-max-ref/v1` 的 paired-end 小切片验收（实验工具；不是最终格式）
- 数据：GIAB/NIST `SRR14724532` R1/R2 各 298,974 reads；完整 GRCh38（UCSC hg38）
- 输入/归档 SHA、命令和口径均在本文件固定
- 相关：[R1 单端基线](2026-11-15-real-wgs-srr14724532.md)、
  [一页纸总结](../max-ratio-one-pager.md)、
  [OpenSpec 变更](../../openspec/changes/2026-11-15-max-ratio-paired-end/design.md)

## 1. 结论

| 方案 | 字节 | 相对原始 FASTQ |
|---|---:|---:|
| R1 单端 `fqmr` | 4,547,935 | 21.974× |
| R2 单端 `fqmr` | 5,130,877 | 19.477× |
| 两个单端归档之和 | **9,678,812** | **20.650×** |
| **paired 联合归档** | **8,825,785** | **22.646×** |

paired 相对两个单端之和：

- 节省 **853,027 B（8.813%）**；
- 归档从 20.650× raw 提升到 **22.646× raw**；
- 收益主要来自 alignment 流，不是质量流。

外部基线（R1+R2 分别压缩后相加）：

| 方案 | 字节 | 相对原始 FASTQ |
|---|---:|---:|
| gzip -6 | 42,773,624 | 4.673× |
| zstd -19 | 33,412,892 | 5.982× |
| 当前 fast 路径 `fqc-sequential/v2` | 34,263,156 | 5.833× |
| CRAM（R1/R2 两个独立 alignment container） | 12,423,444 | 16.088× |
| 两个单端 `fqmr` | 9,678,812 | 20.650× |
| **paired `fqmr`** | **8,825,785** | **22.646×** |

`CRAM` 是 alignment container，不是 FASTQ 字节归档；上表是两个独立 CRAM 文件的
大小之和，不是 paired CRAM，也不是功能等价比较。

### 1.1 三路字节账

| 流 | 两个单端之和 | paired | 变化 |
|---|---:|---:|---:|
| ID | 7,399 | **6,880** | −519（−7.0%） |
| quality | 4,709,728 | **4,711,904** | +2,176（+0.046%） |
| alignment | 4,939,473 | **4,094,927** | **−844,546（−17.10%）** |
| 容器/块头/checksum 等 | 22,212 | **12,074** | −10,138 |
| 合计 | **9,678,812** | **8,825,785** | **−853,027（−8.81%）** |

paired 归档的 `mate_deltas=277,453 / 298,974 = 92.80%`：绝大多数 R2 primary
alignment 与 R1 在同一 contig，因此 R2 的 `targetStart` 改存相对 R1 的 zigzag delta，
不再重复存 contig id 和绝对坐标。

paired 统计：

```text
blocks=23 records=597948 pairs=298974 bases=90290148
mapped=590436 literal=7512 clipped=24598 indel=20393
ids=6880 qualities=4711904 alignments=4094927
fqz_blocks=23 zstd_quality_blocks=0 front_id_blocks=23
mate_deltas=277453 input=199868160 output=8825785 raw/archive=22.646x
```

两个单端之和的 mapped/literal/clipped/indel 为
`590,436 / 7,512 / 24,598 / 20,393`，与 paired 完全一致。paired 没有改变
mapped 判定，只是改变了坐标表示和容器组织。

## 2. 数据身份与范围

完整 run 的 ENA 元数据：

| 字段 | 值 |
|---|---|
| accession | `SRR14724532` |
| study | `PRJNA734598`（NIST Genome in a Bottle） |
| instrument | Illumina NovaSeq 6000 |
| library | WGS / GENOMIC / RANDOM |
| layout | PAIRED，2×150，约 35× |
| spots | 355,791,257 |
| bases | 107,448,959,614（双端合计） |

本切片只取 run 头部：

- R1：gzip 前 21,110,784 B，裁剪为 298,974 条完整 4 行记录，99,934,080 B；
- R2：gzip 前 40,000,000 B，裁剪为同样的 298,974 条记录，99,934,080 B；
- R1/R2 read ID 全部对齐；R1 comment 为 `1/1`，R2 comment 为 `2/2`；
- 参考和 PAF 均为 external：参考不计入归档，PAF 只用于压缩、不进入解压依赖。

R2 的 40,000,000 B 是一个截断 gzip 流；`gzip -dc` 到尾部会报 unexpected EOF，
因此命令使用 `2>/dev/null`，只保留前 298,974 条完整记录。这是固定字节切片，不是完整
gzip 文件。

工具版本：minimap2 2.31-r1302、samtools 1.21（htslib 1.21）、gzip 1.12、
zstd 1.5.6；FQC 为 2026-11-15 的 clang-debug 构建（Debug，只作运行事实，不是 release
速度口径）。

## 3. 固定输入与 SHA

| 文件 | 字节 | SHA-256 |
|---|---:|---|
| `SRR14724532_1.head21m.fastq.gz` | 21,110,784 | `286836d9d79ad8470ddcd2b395562aa4f25a347e8b424a9bb1ad5c159378b95b` |
| `SRR14724532_R1.head.fastq` | 99,934,080 | `d4f6c8bee191ee8cc5207cca5fb86659a2aaa02e3f493b35f2ed50027ab6e0f4` |
| `SRR14724532_2.head40m.fastq.gz` | 40,000,000 | `53644acf28e7cfc132f13beb7fde816ef26b8cc1b66480211b48e8b878cf32ee` |
| `SRR14724532_R2.head.fastq` | 99,934,080 | `546a6986830155f700dcead34e9cbff0291596a4e7887d760a3cd26467ee72e3` |
| `SRR14724532_R1.head.paf` | 49,043,976 | `c33900ac67b0e0d5bad2297767477ecef5408e6cea0313318c44866c4983c935` |
| `SRR14724532_R2.head.paf` | 48,788,417 | `46774f8584f240677420a2c86b0aa69d1e14fe658abeccf8b4fa2ba09d68c784` |
| `SRR14724532_R2.head.fqmr` | 5,130,877 | `2528b4cca24afb4298eec40c92d5163f94608c88ae746058199c95dd9ba9fb7f` |
| `SRR14724532.paired.head.fqmr` | **8,825,785** | **`09cfec68802291c6f4342e3ddee338ed5d3dc5e6b404755895b00e28a58dd4e5`** |
| 恢复 R1 FASTQ | 99,934,080 | `d4f6c8bee191ee8cc5207cca5fb86659a2aaa02e3f493b35f2ed50027ab6e0f4` |
| 恢复 R2 FASTQ | 99,934,080 | `546a6986830155f700dcead34e9cbff0291596a4e7887d760a3cd26467ee72e3` |

外部对照文件：

| 文件 | 字节 | SHA-256 |
|---|---:|---|
| `r2.gzip6.gz` | 21,676,462 | `ee5b0550a3aa4f8b91882e558f21c5f866d9b7a115a5a284278ddaa3eeec988b` |
| `r2.zstd19.zst` | 16,949,483 | `45bc2bc9dfe306c10b61cb0a830e4c12f81640c030494aa8467d1e047393621b` |
| `SRR14724532_R2.head.fqc` | 17,451,743 | `07ed241ae682f1fc216e7807b6b151c3a2842537f7d37ee409e25a64f77e2591` |
| `SRR14724532_R2.head.cram` | 6,494,171 | `fff804699c85bb6d6fe25b0b649fb184230ebe3082786136c737a7d0928bc3bf` |

R1 的 gzip/zstd/FQC/CRAM 对照与 SHA 见
[R1 单端基线](2026-11-15-real-wgs-srr14724532.md)。

## 4. 复现命令

### 4.1 R1/R2 头部

```bash
OUT=/tmp/fqc-real-wgs-20261115/data
mkdir -p "$OUT"

curl -L --fail -r 0-21110783 \
  -o "$OUT/SRR14724532_1.head21m.fastq.gz" \
  https://ftp.sra.ebi.ac.uk/vol1/fastq/SRR147/032/SRR14724532/SRR14724532_1.fastq.gz
gzip -dc "$OUT/SRR14724532_1.head21m.fastq.gz" 2>/dev/null \
  | head -n 1195896 > "$OUT/SRR14724532_R1.head.fastq"

curl -L --fail -r 0-39999999 \
  -o "$OUT/SRR14724532_2.head40m.fastq.gz" \
  https://ftp.sra.ebi.ac.uk/vol1/fastq/SRR147/032/SRR14724532/SRR14724532_2.fastq.gz
gzip -dc "$OUT/SRR14724532_2.head40m.fastq.gz" 2>/dev/null \
  | head -n 1195896 > "$OUT/SRR14724532_R2.head.fastq"
```

### 4.2 完整 hg38 与 minimap2

```bash
curl -L --fail -o "$OUT/hg38.fa.gz" \
  https://hgdownload.soe.ucsc.edu/goldenPath/hg38/bigZips/hg38.fa.gz
gzip -dc "$OUT/hg38.fa.gz" > "$OUT/hg38.fa"
MM=/tmp/fqc-real-wgs-20261115/tools/minimap2-2.31_x64-linux/minimap2
$MM -d "$OUT/hg38.mmi" "$OUT/hg38.fa" -t 12

for mate in R1 R2; do
  $MM -x sr -c --secondary=no -t 12 "$OUT/hg38.mmi" \
    "$OUT/SRR14724532_${mate}.head.fastq" > "$OUT/SRR14724532_${mate}.head.paf"
done
```

### 4.3 单端与 paired 归档

```bash
BIN=./build/clang-debug/tools/fqc_ref_archive

"$BIN" compress -i "$OUT/SRR14724532_R2.head.fastq" \
  --paf "$OUT/SRR14724532_R2.head.paf" -r "$OUT/hg38.fa" \
  -o "$OUT/SRR14724532_R2.head.fqmr" \
  --block-bases 4000000 --max-mismatches 16 -f

"$BIN" compress -i "$OUT/SRR14724532_R1.head.fastq" \
  --paf "$OUT/SRR14724532_R1.head.paf" \
  --mate "$OUT/SRR14724532_R2.head.fastq" \
  --mate-paf "$OUT/SRR14724532_R2.head.paf" \
  -r "$OUT/hg38.fa" \
  -o "$OUT/SRR14724532.paired.head.fqmr" \
  --block-bases 4000000 --max-mismatches 16 -f

"$BIN" verify -i "$OUT/SRR14724532.paired.head.fqmr" -r "$OUT/hg38.fa"
"$BIN" decompress-paired -i "$OUT/SRR14724532.paired.head.fqmr" -r "$OUT/hg38.fa" \
  --r1 "$OUT/SRR14724532.paired.restored.R1.fastq" \
  --r2 "$OUT/SRR14724532.paired.restored.R2.fastq" -f
cmp "$OUT/SRR14724532_R1.head.fastq" "$OUT/SRR14724532.paired.restored.R1.fastq"
cmp "$OUT/SRR14724532_R2.head.fastq" "$OUT/SRR14724532.paired.restored.R2.fastq"
```

### 4.4 外部基线

```bash
gzip -6 -n -c "$OUT/SRR14724532_R1.head.fastq" > "$OUT/reads.gzip6.gz"
gzip -6 -n -c "$OUT/SRR14724532_R2.head.fastq" > "$OUT/r2.gzip6.gz"
zstd -19 -q -f -o "$OUT/reads.zstd19.zst" "$OUT/SRR14724532_R1.head.fastq"
zstd -19 -q -f -o "$OUT/r2.zstd19.zst" "$OUT/SRR14724532_R2.head.fastq"
./build/clang-debug/src/fqc compress -i "$OUT/SRR14724532_R1.head.fastq" \
  -o "$OUT/SRR14724532_R1.head.fqc" --force
./build/clang-debug/src/fqc compress -i "$OUT/SRR14724532_R2.head.fastq" \
  -o "$OUT/SRR14724532_R2.head.fqc" --force

for mate in R1 R2; do
  $MM -a -x sr --secondary=no -t 12 "$OUT/hg38.mmi" \
    "$OUT/SRR14724532_${mate}.head.fastq" > "$OUT/SRR14724532_${mate}.head.sam"
  samtools sort -@ 4 -m 1G -o "$OUT/SRR14724532_${mate}.head.bam" \
    "$OUT/SRR14724532_${mate}.head.sam"
  samtools view -C -T "$OUT/hg38.fa" -@ 4 \
    -o "$OUT/SRR14724532_${mate}.head.cram" "$OUT/SRR14724532_${mate}.head.bam"
  samtools index "$OUT/SRR14724532_${mate}.head.cram"
done
```

### 4.5 一键复现

```bash
# 默认复用已有文件；FQC_BENCH_REPRO=1 会额外重压 paired 归档并 cmp。
FQC_BENCH_PAIRED=1 FQC_BENCH_REPRO=1 ./scripts/fetch_real_wgs_ref_bench.sh
```

## 5. 格式扩展与边界

paired 扩展仍使用 magic `FQCMR1\r\n` 和 version 1：

- 全局头 flags bit0 表示 paired；旧单端归档 flags=0，新解码器仍可读；
- 一个 block 内按 `R1, R2, R1, R2` 交错，paired block 的记录数必须为偶数；
- 第二条 mate 的 per-read flags bit4 表示 `MATE_DELTA`；
- 只有 R1/R2 都 mapped、同一 contig 时才写 delta；否则写绝对 contig/target；
- delta 使用 zigzag varint；解压拒绝没有 mapped first mate 的 delta、负向/正向越界和
  奇数 paired block；
- ID 仍逐条存；当前没有跨 mate 共享 ID、联合质量模型或跨 mate 重排；
- 旧单端编码布局与旧归档字节不变；用新代码重压 R1 头部，归档大小与 SHA
  `6f50a3146e018c33fffe6be3a21af9163b8ae1223d1b386f6f440507bb3b647a` 均与旧归档一致。

这意味着 22.646× 是“paired 容器 + R2 坐标 delta”的结果，不是完整 paired-end
质量/ID 联合模型。质量流在 paired 中反而比两个单端之和 **大 2,176 B（+0.046%）**，
说明把 R1/R2 交错后 fqzcomp 的块内上下文几乎中性，当前主要收益来自 alignment。

### 5.1 口径

- 主口径：external reference + external PAF 不计入，`199,868,160 / 8,825,785 = 22.646×`；
- 计入 PAF：`199,868,160 / (8,825,785 + 97,832,393) = 1.874×`；
- 计入完整 GRCh38：`199,868,160 / (8,825,785 + 3,273,481,150) = 0.0609×`；
- 不是完整 35× run，不是人类 30× 产品结论；
- 没有 ORA/Genozip/SPRING 同数据复测；
- CRAM 是 alignment container，不是 FASTQ 字节归档；
- 没有 `S/H/N/P`、长读 spliced alignment、BAM/CRAM sidecar、内置 mapper、参考嵌入、
  SHA-256、原子输出或内存上限；
- 速度数字只作运行事实：R2 单端 debug 压缩 3:33，paired debug 压缩 6:25；不做速度结论。

## 6. 验证矩阵

| 检查 | 结果 |
|---|---|
| `ref_archive_test` | 14/14 cases passed（新增 paired round-trip、ID mismatch、接口误用） |
| 全量 `clang-debug` | 22/22 passed |
| 全量 `clang-asan` | 22/22 passed；ASan/UBSan 无报告 |
| `format-check` | clang-format-18 passed |
| paired `verify` | 通过；块/footer/滚动校验和一致 |
| paired `decompress-paired` + `cmp` | R1/R2 均逐字节一致 |
| 重复 paired 压缩 | 两次归档逐字节一致；SHA 相同 |
| 旧 R1 单端归档 | 新解码器 `verify` 通过 |
| 旧单端字节兼容 | 新代码重压 R1，与旧归档逐字节 `cmp` 一致；SHA 均为 `6f50a314…b647a` |
| `mate_deltas` | 277,453 / 298,974（92.80%） |
| 外部基线 | gzip/zstd/FQC/CRAM 已固定 SHA |
| 一键复现 | `FQC_BENCH_PAIRED=1 ./scripts/fetch_real_wgs_ref_bench.sh` 退出 0；R1/R2 `verify`/`cmp`、paired `verify`/`decompress-paired`/`cmp` 与外部基线均通过；本轮输出固定 SHA 与上表一致 |

## 7. 后续

1. 完整 30× paired run，报告参考计入/不计入两套数字；
2. ORA/Genozip 同数据同口径比较；
3. 评估跨 mate 共享 ID、质量上下文和 insert-size 模型；
4. 最后才是速度、内存、正式格式和原子输出。

本文件只证明：**在真实 WGS R1/R2 头部小切片上，paired 容器和 R2 相对坐标编码能
稳定、无损地把归档从两个单端之和的 20.650× 提升到 22.646×。**
