# fqc_ref_archive

Phase 2 `max-ref` 的**开发/验收专用**参考压缩原型；Phase 2.1 增加了自适应 ID
front coding，Phase 2.2 增加了 clip/indel CIGAR，Phase 2.3 增加了 paired-end 容器与
R2 相对坐标 delta。它验证一个核心结构：

```text
FASTQ + PAF sidecar + external reference FASTA
  → 位置/方向/mismatch/clip/indel 描述（未映射 read 回退 literal）
  → ID / quality / alignment 三路独立压缩
  → fqc-max-ref/v1 归档
```

它不是最终 `--max-ratio` CLI，不安装、不进入 `fqc` 主命令，也不改变
`fqc-sequential/v2`。当前实现使用外部 PAF 作为比对 sidecar，不内置 mapper，不支持 BAM。

## 构建

```bash
./scripts/build.sh clang-debug
./build/clang-debug/tools/fqc_ref_archive --help
```

目标由 `tools/CMakeLists.txt` 默认构建；测试为 `ref_archive_test`。

## 用法

```bash
# 压缩：FASTQ + PAF + 外部参考
./build/clang-debug/tools/fqc_ref_archive compress \
  -i reads.fastq --paf reads.paf -r reference.fa \
  -o reads.fqmr --block-bases 4000000 --max-mismatches 16 -f

# 完整解码并校验，不写 FASTQ
./build/clang-debug/tools/fqc_ref_archive verify -i reads.fqmr -r reference.fa

# 解压
./build/clang-debug/tools/fqc_ref_archive decompress \
  -i reads.fqmr -r reference.fa -o restored.fastq -f

# paired 压缩：R1/R2 锁步，R2 同 contig 时存相对 R1 的坐标 delta
./build/clang-debug/tools/fqc_ref_archive compress \
  -i R1.fastq --paf R1.paf \
  --mate R2.fastq --mate-paf R2.paf \
  -r reference.fa -o paired.fqmr \
  --block-bases 4000000 --max-mismatches 16 -f

# paired 分离解压
./build/clang-debug/tools/fqc_ref_archive decompress-paired \
  -i paired.fqmr -r reference.fa --r1 restored.R1.fastq --r2 restored.R2.fastq -f
```

`--paf` 必须是带 `cg:Z:` 的 PAF（例如
`minimap2 -x sr -c ref.fa reads.fastq > reads.paf`）。当前接受：

- `M/=/X` 线性对齐；
- `I`（query 多出的碱基）和 `D`（参考多出的碱基）；
- query 不覆盖整条 read 的左右 clip（由 `queryStart/queryEnd` 表达）；
- `queryStart < queryEnd <= readLength`，CIGAR 的 query/target 消费量必须与 PAF 坐标一致。

`S/H/N/P`、空 CIGAR、零长度、超过 256 个操作或坐标不一致的 read 自动回退 literal，不报错。
`--max-mismatches` 只限制 `M/=/X` 内的替换数，indel 不是替换。参考 FASTA 的 soft-mask
小写 `a/c/g/t` 在加载时统一为大写；mismatch、CIGAR 重建和参考摘要都使用归一化序列。

## 格式摘要

- magic：`FQCMR1\r\n`（与 v2 / `fqc-max/v1` 不同）；
- 全局头：52 字节固定字段 + contig 名称/长度表；flags bit0 表示 paired；
- 块头：88 字节；三路载荷分别为 ID、quality、alignment；paired block 内按
  `R1, R2, R1, R2...` 交错，记录数必须为偶数；
- ID：每块在 raw 与 front-coded 两种逻辑流之间自适应选择，外层 zstd19；
  raw 为 `varint(recordCount)` + 每记录 `varint(idLen)+id+varint(commentLen)+comment`；
  front-coded 为每条记录相对上一条的 id/comment 公共前缀长度 + 后缀；
  `idCodec=1` 表示 raw，`idCodec=2` 表示 front-coded；
- quality：每块在 fqzcomp（策略 0/1）与 zstd19 之间取较小者；
- alignment：每记录 flags/长度；纯全长单 M 的 mapped 记录仍存 contig id、target offset、
  mismatch 数、mismatch delta/alt；含 clip 或 I/D 的 mapped 记录使用 flags bit3 扩展，
  额外存 query 范围、CIGAR 操作、I 的 base code、左右 clip 和 aligned-query mismatch；
  paired 的第二条 mate 在双方 mapped 且同 contig 时使用 flags bit4 `MATE_DELTA`，存
  zigzag 相对 R1 的 target offset 并省略 contig id；literal 记录存原始序列，外层 zstd19；
- 校验：块级 XXH64（覆盖三路原始逻辑流）+ footer 滚动全局校验和；参考身份存 XXH64
  contig/序列摘要；
- 解压必须有同一外部参考；摘要或 contig 表不匹配 fail closed。

外部参考**不计入归档体积**；归档只存 contig 名称/长度和参考摘要。当前参考摘要仍是
XXH64，不是最终设计要求的 SHA-256，不能把它当密码学身份认证。

## 实测与边界

完整命令、输入 SHA、归档 SHA、mapped/literal/clipped/indel 计数与复现实验见：

- Phase 2 基础：[`docs/max-ratio-baselines/2026-11-15-ref-container.md`](../../docs/max-ratio-baselines/2026-11-15-ref-container.md)；
- Phase 2.2 CIGAR：[`docs/max-ratio-baselines/2026-11-15-ref-cigar.md`](../../docs/max-ratio-baselines/2026-11-15-ref-cigar.md)；
- 真实 WGS R1 + 完整 hg38：[`docs/max-ratio-baselines/2026-11-15-real-wgs-srr14724532.md`](../../docs/max-ratio-baselines/2026-11-15-real-wgs-srr14724532.md)；
- 真实 WGS paired R1+R2：[`docs/max-ratio-baselines/2026-11-15-real-wgs-paired.md`](../../docs/max-ratio-baselines/2026-11-15-real-wgs-paired.md)。

合成数据上 Phase 2.2 的 R1/R2 为 **792,217 / 796,633 B**，约 **21.993× / 21.871× raw**；
mapped 分别为 49,998/50,000，literal 2/0，clipped 51/44，indel 753/745；`verify` + `cmp`
通过。真实 WGS R1 头部 298,974 reads + 完整 hg38 上，归档 **4,547,935 B（21.974× raw）**，
mapped 296,343、literal 2,631；quality/alignment/ID 为 2,181,008/2,352,120/3,701 B。
paired 扩展在 R1/R2 各 298,974 reads 上，把两个单端归档之和 9,678,812 B（20.650× raw）
降到 **8,825,785 B（22.646× raw）**，节省 853,027 B（8.813%）；alignment 流
4,939,473 → 4,094,927 B（−17.10%），quality 流 4,709,728 → 4,711,904 B（+0.046%），
`mate_deltas=277,453/298,974`（92.80%）。用新代码重压 R1 头部得到 4,547,935 B，SHA
`6f50a3146e018c33fffe6be3a21af9163b8ae1223d1b386f6f440507bb3b647a`，与旧单端归档
逐字节一致。这些都是参考外部、PAF 外部、非完整 30× WGS 的机制/小切片结果，不能直接
外推为产品比例。复现脚本可用 `FQC_BENCH_PAIRED=1`（可选 `FQC_BENCH_REPRO=1`）运行
paired 链路。

ID codec 的真实验证、结构化 ID 合成实验与兼容性回归见
[`docs/max-ratio-baselines/2026-11-15-id-codec.md`](../../docs/max-ratio-baselines/2026-11-15-id-codec.md)；
原始 wgsim ID 会自动回退 raw，归档与改动前逐字节一致。

未实现/未承诺：

- 内置 mapper、BAM sidecar、参考嵌入；
- `S/H/N/P`、长读 spliced alignment；
- 跨 mate 共享 ID、联合质量上下文、insert-size 模型；
- 完整 30× paired run、ORA/Genozip 同数据复测；
- `+` 行内容、CRLF/混合行尾、空 read 的逐字节保真（仍走生产 `FastqParser`，
  输出 canonical LF FASTQ）；
- 原子输出、并发、内存上限与速度门禁；
- SHA-256 参考身份、正式 `openspec` 格式冻结。

开发测试：

```bash
ctest --test-dir build/clang-debug -R ref_archive_test --output-on-failure
```
