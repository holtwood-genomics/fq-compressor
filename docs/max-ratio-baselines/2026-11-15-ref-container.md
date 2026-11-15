# 2026-11-15 参考压缩原型 `fqc-max-ref/v1` 实测

- 状态：Phase 2 `max-ref` 实验原型 + Phase 2.1 自适应 ID codec 扩展（**不是**最终
  max-ratio，不是产品 CLI）
- 实现：`tools/ref_archive/`，目标 `fqc_ref_archive`
- 参考模式：**external reference，不计入归档体积**
- 比对 sidecar：带 `cg:Z:` 的 PAF（当前只接受纯 `<len>M`、全长、无剪切）
- 质量 codec：htscodecs `v1.6.7` fqzcomp（每块策略 0/1 自适应，zstd19 回退）
- 设计：[docs/max-ratio-design.md](../max-ratio-design.md)
- ID codec 增补：[2026-11-15-id-codec.md](2026-11-15-id-codec.md)
- 工具说明：[tools/ref_archive/README.md](../../tools/ref_archive/README.md)
- 格式契约：[openspec/changes/2026-11-15-max-ratio-phase2-ref/design.md](../../openspec/changes/2026-11-15-max-ratio-phase2-ref/design.md)

本文件记录**实际写出的 `fqc-max-ref/v1` 归档**。数据是确定性合成的 5 Mb 随机参考 +
wgsim 3× 双端短读；它证明参考位置/差异编码与外部参考校验链路可工作，不能代表人类 30×
WGS 的最终压缩比。

## 1. 结论

| 数据 | 原始 FASTQ | `fqc-max-ref/v1` | raw / archive | mapped / literal | verify + cmp |
|---|---:|---:|---:|---:|---|
| 合成 R1 | 17,423,235 B | **827,688 B** | **21.050×** | 49,194 / 806 | 通过 |
| 合成 R2 | 17,423,235 B | **827,546 B** | **21.054×** | 49,211 / 789 | 通过 |

关键结论：

1. **位置/方向/mismatch 编码确实能把参考区域内的序列成本压到很低。** R1 的 98.39%
   read 走 mapped，只有 806 条回退 literal；R2 为 98.42% / 789 条。
2. **归档的主要字节已经不是序列。** R1 中 ID 流 496,574 B、alignment 流 330,759 B、
   quality 流仅 74 B；合成质量全是 Q23 单符号，因此 quality 数字不能外推到真实数据。
   原始 wgsim ID 是随机长串，自适应 ID codec 选择 raw zstd；结构化 ID 的独立实验见
   [2026-11-15-id-codec.md](2026-11-15-id-codec.md)。
3. **参考是外部依赖。** 归档只存参考 contig 名称/长度与 XXH64 摘要；5,071,439 B 的参考
   FASTA 不计入归档。若把参考计入，比例会显著下降；正式报告必须单列口径。
4. **这不是 ORA/Genozip 等价物。** 没有内置 mapper、BAM sidecar、clip/indel 编码、数字
   ID token/delta、嵌入参考、逐字节 `+` 行/CRLF/空 read 边界；21× 只说明结构方向正确。
   通用 ID front coding 已实现，详见 [2026-11-15-id-codec.md](2026-11-15-id-codec.md)。
5. **速度数字只作方向性记录。** Debug 构建、单线程、zstd19，R1 约 8.4 s、R2 约 6.3 s；
   Phase 4 之前不设吞吐门禁。

## 2. 合成数据与固定 SHA-256

所有文件位于 `/tmp/fqc-ref-bench-20261115/`；命令在 2026-11-15 实际执行。

### 2.1 参考

```bash
python3 - <<'PY'
import random
from pathlib import Path
rng = random.Random(20261115)
seq = ''.join(rng.choice('ACGT') for _ in range(5_000_000))
out = Path('/tmp/fqc-ref-bench-20261115/ref5m.fa')
with out.open('w') as f:
    f.write('>chrSyn5M\n')
    for i in range(0, len(seq), 70):
        f.write(seq[i:i+70] + '\n')
PY
```

| 文件 | 字节 | SHA-256 |
|---|---:|---|
| `ref5m.fa` | 5,071,439 | `df2ce24106ef8ab086e74c001a8b01570403e8f2c7d29831ba9a4c63a1ff543b` |

### 2.2 reads 与 PAF

```bash
wgsim -N 50000 -1 150 -2 150 -S 20261115 \
  -e 0.005 -d 500 -s 50 -r 0.001 -R 0.15 -X 0.3 \
  ref5m.fa r1.fq r2.fq

minimap2 -x sr -c -t 4 ref5m.fa r1.fq > r1.paf
minimap2 -x sr -c -t 4 ref5m.fa r2.fq > r2.paf
```

| 文件 | 字节 | SHA-256 |
|---|---:|---|
| `r1.fq` | 17,423,235 | `ca35dbdf9dc7886eac5f05bec1184f1ab2cedffcb9959109eb524eeaf6f572bb` |
| `r2.fq` | 17,423,235 | `c3dcae4be810487baf319764a9e0abfe588b1fbf920d13d8b2e435c3d38808ca` |
| `r1.paf` | 9,445,200 | `b50e56723d1994862d23e2de521a27cedef7092c4125c5e9dcfcd84e8f8c7a74` |
| `r2.paf` | 9,446,372 | `8851a952e70e2de34fcc992b886b5e46b2adf71329c772bd5de0b0f732b987d1` |

- 每端 50,000 reads × 150 bp = 7,500,000 bases；双端合计 15,000,000 bases，对 5 Mb
  参考约 3× 覆盖。
- 质量行为单符号 `8`（Phred+33 = Q23），不是真实测序质量分布。
- 参考是随机序列，没有重复区域/低复杂度；真实基因组会改变 mapping 候选和 ID 结构。
- PAF 只用于压缩；解压不需要 PAF，但需要同一参考 FASTA。

## 3. 压缩、验证与解压

```bash
BIN=./build/clang-debug/tools/fqc_ref_archive
BENCH=/tmp/fqc-ref-bench-20261115

for mate in r1 r2; do
  "$BIN" compress -i "$BENCH/$mate.fq" --paf "$BENCH/$mate.paf" \
    -r "$BENCH/ref5m.fa" -o "$BENCH/$mate.fqmr" \
    --block-bases 4000000 --max-mismatches 16 -f

  "$BIN" verify -i "$BENCH/$mate.fqmr" -r "$BENCH/ref5m.fa"

  "$BIN" decompress -i "$BENCH/$mate.fqmr" -r "$BENCH/ref5m.fa" \
    -o "$BENCH/$mate.restored.fastq" -f
  cmp "$BENCH/$mate.fq" "$BENCH/$mate.restored.fastq"
done
```

R1 的 `compress` / `verify` 输出（`verify` 的 input/output 方向与压缩相反）：

```text
blocks=2 records=50000 bases=7500000 mapped=49194 literal=806
ids=496574 qualities=74 alignments=330759
fqz_blocks=2 zstd_quality_blocks=0 front_id_blocks=0 input=17423235 output=827688 raw/archive=21.050x
```

R2：

```text
blocks=2 records=50000 bases=7500000 mapped=49211 literal=789
ids=496992 qualities=74 alignments=330199
fqz_blocks=2 zstd_quality_blocks=0 front_id_blocks=0 input=17423235 output=827546 raw/archive=21.054x
```

### 3.1 归档字节

| 项 | R1 | R2 |
|---|---:|---:|
| 原始 FASTQ | 17,423,235 B | 17,423,235 B |
| `fqc-max-ref/v1` | **827,688 B** | **827,546 B** |
| raw / archive | **21.050×** | **21.054×** |
| archive SHA-256 | `23fe0fd1ef9cfef84c60195b6a86b79e31d2159f59f5c5a751d95b61ee42b2d5` | `f5c1b38dec22f952d3d6fd723a50fd5e6272d0b8614ceb881e2d34d62c22270d` |
| 块数 | 2 | 2 |
| records / bases | 50,000 / 7,500,000 | 50,000 / 7,500,000 |
| mapped / literal | 49,194 / 806 | 49,211 / 789 |
| ID 流 | 496,574 B | 496,992 B |
| quality 流 | 74 B | 74 B |
| alignment 流 | 330,759 B | 330,199 B |
| fqzcomp / zstd 质量块 | 2 / 0 | 2 / 0 |

R1 重复压缩输出逐字节一致（同一 SHA）；当前归档字节是可复现的。

### 3.2 验证矩阵

| 检查 | 结果 |
|---|---|
| `ref_archive_test` | 8/8 passed（见 §5） |
| 全量 `clang-debug` / `clang-asan` | 20/20 passed；ASan/UBSan 无报告 |
| `verify` R1/R2 | 通过；计数、footer、滚动校验和一致 |
| `decompress` + `cmp` R1/R2 | 通过；canonical LF FASTQ 逐字节一致 |
| 重复压缩 R1 | SHA 一致 |
| 参考 SHA/contig 表不匹配 | `ref_archive_test` 覆盖，返回 checksum 错误 |
| 截断/尾部多余字节 | 测试覆盖，fail closed |
| 空输入 | 测试覆盖，header + footer，0 blocks |

## 4. 格式与实现边界

当前原型：

- magic `FQCMR1\r\n`，全局头 52 字节 + contig table；块头 88 字节；footer 40 字节；
- ID：raw id/comment 逻辑流或 front-coded 逻辑流，二者各自 zstd19 后按块取较小者；
  `idCodec=1` 为 raw，`idCodec=2` 为 front-coded；尾部 4 字节保存 front-coded
  逻辑流未压缩长度（raw 时写 0）；block checksum 仍覆盖解码后的 raw ID 流。新解码器
  可读旧 raw 归档，但旧工具会拒绝 `idCodec=2` 的新归档（单向扩展）；
- quality：fqzcomp 策略 0/1 vs zstd19 每块取较小者；
- alignment：每记录 flags、read length；mapped 记录 contig id/target offset/mismatch
  count/mismatch delta+alt；literal 记录原始序列，外层 zstd19；
- 参考身份：contig 名/长度/序列的 XXH64 摘要；解压时参考必须逐 contig 对齐；
- 每块 XXH64 覆盖 ids/qualities/alignments 原始逻辑流，footer 存滚动全局校验和。

明确未实现：

- **内置 mapper**：必须外部提供 PAF；PAF 本身不计入归档，也不是解压依赖。
- **BAM sidecar**：没有 BAM/CRAM 连接、name-sort/索引、CIGAR 解码。
- **clip/indel**：`cg` 不是纯 `<len>M` 的 read 走 literal；因此当前 mapped 率是
  “纯替换、全长线性比对”的 mapped 率。
- **参考嵌入**：只有 external reference；参考 FASTA 不计入归档体积。
- **SHA-256**：原型用 XXH64；正式格式要求参考身份使用 SHA-256。
- **数字 ID token/delta**：通用 front coding 已实现；没有数字 token、字段字典或跨块模型。
- **paired-end 联合**：R1/R2 是两个独立归档；没有跨 mate 的位置/质量模型。
- **逐字节边界**：仍走生产 `FastqParser`，输出 canonical LF FASTQ；`+` 行内容、
  CRLF/混合行尾、空 read 未保真。
- **原子输出/内存上限/并发**：原型直接写目标文件，单线程，未设 maxRSS 门禁。

## 5. 测试

```bash
./scripts/build.sh clang-debug
ctest --test-dir build/clang-debug -R ref_archive_test --output-on-failure
```

新增/覆盖：

1. mapped（正/负链）+ mismatch + literal 混合 round-trip；
2. 参考摘要不匹配拒绝；
3. 截断归档拒绝；
4. 多块（每 read 一块）+ `--max-mismatches 0` 时 mismatch/indel read 回退 literal；
5. 空 FASTQ → 0 block 归档；
6. footer 后多余字节拒绝；
7. 输出路径等于输入/参考时拒绝；
8. 结构化 ID 强制走 front coding，并完成 `verify` / `cmp`。

## 6. 口径与不可外推

- **21× 不是人类 WGS 结论。** 合成参考随机、无重复、无低复杂度、3× 覆盖、恒定质量；
  真实 30× WGS 的 ID、质量、重复区域和 mapping 失败率都会改变比例。
- **53.385× 是结构化 ID 合成机制验证。** 只替换 ID/PAF query name，序列/mapping/质量
  不变；参考仍不计入，不能当作真实 WGS 结果。详见
  [2026-11-15-id-codec.md](2026-11-15-id-codec.md)。
- **外部参考不计入。** 若把 5,071,439 B 参考计入，R1 的 raw/(archive+reference) 约
  2.95×；这不是 ORA/Genozip 的“嵌入参考”口径。
- **PAF 是压缩输入，不是归档依赖。** 不把 9.4 MB PAF 计入归档，也不声称“压缩端零成本”；
  压缩时间/内存包含加载 PAF 和参考。
- **mapped 率高不代表真实数据。** wgsim + 单染色体随机参考 + minimap2 `-x sr` 是受控
  场景；人类重复区域、indel、soft clip、低 MAPQ 都会让 literal 比例上升。
- **质量流几乎为零。** 单符号质量让 fqzcomp 只剩模型头；真实数据应以 Phase 1 的
  HiSeq/NovaSeq 结果为参考，而不是本表的 74 B。
- 与 ORA/Genozip 的 5–6× vs gzip 不可直接比较：本实验没有 fastq.gz 基线、没有完整
  30× WGS、没有同数据同线程对比。
