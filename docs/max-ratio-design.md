# FQC 最高压缩比方案（设计与实验记录）

> 状态：**设计完成；Phase 0 探针与数据集 manifest、Phase 1 质量 codec 探针、Phase 1
> 实验容器 `fqc-max/v1`、Phase 2 参考原型 `fqc-max-ref/v1`、Phase 2.1 自适应 ID
> front coding、Phase 2.2 clip/indel CIGAR 编码已实现（2026-11-15）。Phase 2 已完成
> “PAF sidecar + external reference”的合成机制验证，以及真实 WGS R1 头部 + 完整 hg38
> 小切片验收（21.974×，external reference 不计入）；完整 30× 人类 WGS、paired-end
> 和 Phase 3（无参伪基因组）未实现。**
> 默认 CLI、`fqc-sequential/v2` 格式与 fast 流式路径均不变；`max-compress` 与
> `fqc_ref_archive` 是显式实验工具，不是最终 max-ratio。
> 日期：2026-11-14；最近更新：2026-11-15
> 关联：[ADR-0003](decisions/0003-max-ratio-mode.md)、[调研报告](fastq-compression-survey.md)、
> [路线图](roadmap.md)、[算法文档](../ALGORITHM.md)、
> [Phase 0 OpenSpec 提案](../openspec/changes/2026-11-14-max-ratio-phase0-benchmark/proposal.md)、
> [质量 codec OpenSpec 提案](../openspec/changes/2026-11-15-max-ratio-quality-codec/proposal.md)、
> [数据集 manifest](max-ratio-baselines/datasets.md)、
> [质量 codec 基线](max-ratio-baselines/2026-11-15-quality-codecs.md)、
> [Phase 1 实验容器基线](max-ratio-baselines/2026-11-15-max-container.md)、
> [Phase 2 参考原型基线](max-ratio-baselines/2026-11-15-ref-container.md)、
> [Phase 2.1 ID codec 基线](max-ratio-baselines/2026-11-15-id-codec.md)、
> [Phase 2.2 CIGAR 基线](max-ratio-baselines/2026-11-15-ref-cigar.md)、
> [真实 WGS 基线](max-ratio-baselines/2026-11-15-real-wgs-srr14724532.md)、
> [Phase 1 容器契约](../openspec/changes/2026-11-15-max-ratio-phase1-container/design.md)、
> [Phase 2 参考契约](../openspec/changes/2026-11-15-max-ratio-phase2-ref/design.md)、
> [Phase 2.1 ID codec 契约](../openspec/changes/2026-11-15-max-ratio-phase2-id-codec/design.md)、
> [Phase 2.2 CIGAR 契约](../openspec/changes/2026-11-15-max-ratio-phase2-cigar/design.md)、
> [Phase 2 实现笔记](../.agents/notes/implemented/architecture/2026-11-15-max-ratio-phase2-ref.md)、
> [提案笔记](../.agents/notes/proposed/architecture/2026-11-14-max-ratio-mode.md)

本文回答一个问题：**在不放弃无损的前提下，FQC 要把压缩比从当前约 4×（相对原始 FASTQ）
推到业界第一梯队，应该换什么算法结构？**

本文是设计与证据账本，不是线上格式规范。任何 magic、codec ID、块布局在实现前都必须经过
`openspec/changes/` 变更与 ADR 复审；Phase 1/2 的实现都是**追溯补契约**，只冻结实验格式。
当前仓库有实验性 `fqc-max/v1` 容器、`max-compress` 子命令与开发工具
`fqc_ref_archive`，但仍没有 `--mode max-ratio` 顶层开关或最终格式。

## 1. 一页结论

1. **没有单一“最高压缩比”工具。** 结果取决于是否允许参考基因组、是否允许重排/重编号、
   数据是 HiSeq 全质量还是 NovaSeq 分箱质量、以及比较基准是原始 FASTQ 还是 `fastq.gz`。
2. **绝对最高压缩比路线是有参压缩。** Illumina ORA 与 Genozip 在 2025 年独立 benchmark 中
   分别达到相对 `fastq.gz` 的 5.64× 与 5.99×；厂商示例中 ORA 把 235 GB 原始 FASTQ
   （55 GB gzip）压到 11 GB，约 21× 原始、5× gzip。代价是必须持有参考基因组或参考索引。
3. **无参最高压缩比路线是“全局相似性 + 专用质量编码”。** PgRC2、Mstcom、FQSqueezer、
   SPRING、fqxv 这一类工具通过 minimizer/重叠图/伪基因组/重排吃跨 read 冗余，再对质量值做
   上下文建模。它们通常比流式 2-bit + zstd 高一个数量级，但需要两趟、全局状态、较大内存，
   且部分数字只是 DNA 流 bits/base，不是完整 FASTQ 归档比。
4. **当前 FQC 的 4× 不是调 zstd 档位能补上的。** 本地真实 HiSeq 小样本中，质量流约占归档
   体积 51%，序列流约 42%，ID 约 7.5%；序列已经接近 2-bit 下限，质量流仍是原始 ASCII +
   zstd。继续调 zstd level 只能改变几个百分点，不能跨越到 10× 以上。
5. **建议新增独立的 opt-in `max-ratio` 引擎，而不是改造 fast 路径。** 两条 profile：
   - `max-ref`：有参考基因组/PAF/BAM sidecar 时追求绝对最高压缩比；
   - `max-ref-free`：无参考时用全局伪基因组/差异编码追求通用最高压缩比。
   实现顺序按绝对压缩比优先：Phase 2 先做 `max-ref`，Phase 3 再做 `max-ref-free`。
6. **关键设计选择：全局伪基因组 + 原始顺序引用，不存 permutation。** 读段仍然按原顺序编码，
   但序列可以用全局 contig 的位置/差异表示。这样保留原始顺序，又不必像“先重排再还原”那样
   为每个 read 存排列索引；这是本方案与 SPRING/fqxv 重排路线最重要的结构差异。
7. **质量值优先复用成熟实现，且已有本地证据。** htscodecs fqzcomp 在 HiSeq 全质量、
   NovaSeq RNA-Seq 分箱质量与 WGS 分箱质量三个本地样本上都优于 zstd19（质量流分别小
   17.7%、22.9% 与 23.2%）；
   每块在 fqzcomp 策略 0/1 与 zstd 之间自适应选择，保留回退但不预设分箱质量会输。
8. **先压缩比，后速度。** Phase 0–3 不设吞吐门禁；只有压缩比达到目标后，才讨论并行、
   SIMD、内存复用与解压吞吐。本文不把任何速度数字当作已实现结果。
9. **Phase 2 原型已完成合成与真实 WGS 小切片两条证据，但仍不是产品结论。** 合成
   5 Mb 参考 + wgsim 3× 双端数据上，Phase 2.2 的 R1/R2 归档分别为 792,217 B /
   796,633 B，相对原始 FASTQ 约 **21.993× / 21.871×**；mapped 49,998/50,000，
   literal 2/0，clipped 51/44，indel 753/745。结构化 ID 合成 R1 为 296,082 B
   （**59.781×**）。真实 WGS R1 头部 298,974 reads + 完整 hg38 上，归档
   **4,547,935 B（21.974× raw）**，mapped 296,343、literal 2,631，`verify` + `cmp`
   通过，重复压缩 SHA 一致；但参考/PAF 不计入，仍是单 R1 头部，不是完整 30× WGS。

## 2. 口径与证据分级

### 2.1 三个容易混淆的压缩比

| 口径 | 定义 | 用途 |
|---|---|---|
| 相对原始 FASTQ | `raw FASTQ bytes / archive bytes` | 回答“原始数据缩小多少倍” |
| 相对 `fastq.gz` | `fastq.gz bytes / archive bytes` | 业界论文最常用的比较口径 |
| bits per base（bpb） | `archive bits / sequenced bases` | 只看 DNA 流时常出现；**不等于完整 FASTQ 归档比** |

本文同时标注口径。任何“20×”“25×”如果不说明是相对原始还是相对 gzip、是否含质量/ID/
参考，视为不可比较。

### 2.2 证据等级

全文使用以下标签：

- **【独立 benchmark】**：第三方在固定数据、固定版本上做的对比，优先采信。
- **【论文自报】**：作者自己论文中的数字，方法和数据可能有利于自己的工具。
- **【厂商自报】**：厂商官网/白皮书数字，通常缺少完整可复现实验。
- **【本地实验】**：本仓库本次实际跑出的数字，附数据集 SHA-256 与命令边界。
- **【设计目标】**：尚未实现的验收目标，不是结果。
- **【推断】**：作者基于机制和数据的分析，不作为事实引用。

### 2.3 无损边界

本文的“无损”目标是：

- 对**结构合法的 FASTQ**，压缩-解压后逐字节一致；
- 保留 ID/comment 的原始空格、大小写、IUPAC 符号、空 read、`+` 行内容；
- 保留原始 read 顺序；
- 保留 CRLF/LF 行尾（混合行尾也要能还原，或明确拒绝）。

这与“字段级无损”不同。部分论文工具只保证 sequence+quality 的集合或字段一致，可能丢失
`+` 行 metadata、非 ACGTN 符号、原始 read 名或顺序；本文不把这类工具直接称为“逐字节无损”。
当前 `fqc-sequential/v2` 的已文档化边界是 LF-only 规范 FASTQ；max-ratio 若要扩大边界，必须在
格式规范中单独写清。

### 2.4 参考基因组与有损质量

- 外部参考不计入归档体积时，有参压缩的压缩比会被“放大”；报告必须注明参考是否随归档存储。
- Genozip 的 `--REFERENCE` 会把参考片段存入归档，官方说明约增加 0.23 bytes/reference base，
  人类 WGS 约 +700 MB；`--reference` 是外部参考模式，解压时仍需同一参考。
- Genozip `--optimise`、CoLoRd 的 quality quantization、FQSqueezer 的 8/4/2/none 质量档、
  FastqCA 的 Q4 质量模式都属于有损或有损选项；本文的无损结论不混入这些数字。

## 3. 业界与学术现状

### 3.1 2025 年独立 benchmark

Betschart 等在 *Scientific Reports* 2025 的 benchmark 使用 82 个 GIAB 样本、Illumina
NovaSeq 6000、约 35× 覆盖、双端 `fastq.gz`，8 线程；Genozip 通过 `--reference` 使用参考。
结果中位数如下【独立 benchmark】：

| 工具 | 类型 | 归档大小中位数 | 相对 `fastq.gz` | 压缩时间中位数 | 压缩内存中位数 |
|---|---|---:|---:|---:|---:|
| Genozip 15.0.62 | 有参 | 11.4 GB | **5.99×** | 2151 s | 11.0 GB |
| DRAGEN ORA 4.3.4 | 有参 | 12.1 GB | **5.64×** | 1488 s | 40.0 GB |
| SPRING 1.1.1 | 无参、重排 | 18.0 GB | **3.79×** | 21,663 s | 57.3 GB |
| repaq 0.3.0 | 无参、流式 | 34.4 GB | **1.99×** | 24,101 s | 5.48 GB |
| 输入 `fastq.gz` | — | 68.3 GB | 1.00× | — | — |

这张表的重点是：**Genozip/ORA 的优势来自参考 + 专用模型，不是通用压缩器调参**；SPRING
的重排路线在同数据上仍明显低于有参路线，但仍远高于流式轻量方案。补充材料确认 Genozip
命令包含 `--reference hs38DH.ref.genozip` 和双端 `--pair`；因此不能把 5.99× 当作无参结果。

### 3.2 有参路线：ORA / Genozip

**ORA** 的核心不是“把 FASTQ 丢给 zstd”，而是【厂商自报 + 格式说明】：

1. 用超快 mapper 把 read 映射到参考基因组；
2. 序列编码为参考位置 + 方向 + mismatch/clip 列表；完全匹配的 read 只存位置；
3. 未映射 read 用 2/4 bit 回退；
4. 编码后的序列再用 zstd 压缩；
5. 质量值用 range coder + 上下文模型，按质量分布分组；
6. read name 做数字/非数字 token 化，数字按前一条 read 做 delta，非数字交给 zstd。

官方格式说明给出的序列开销约为 **0.3–0.5 bits/nucleotide**（编码+zstd 后），而原始 FASTQ
是 8 bits/nucleotide、gzip 后约 2 bits/nucleotide。厂商示例：235 GB 原始 FASTQ → 55 GB
gzip → 11 GB ORA，约 21× 原始、5× gzip。ORA 默认参考为 hg38，解压需要同一参考；参考版本
可以与下游分析使用的参考不同，但压缩/解压必须一致。

**Genozip** 用参考文件把序列表示成参考位置或近似匹配，再对其余流做专用编码。官方 FASTQ
示例给出 31 GB `fastq.gz` → 6.0 GB `.genozip`，原始比 24.7×、相对 gzip 5.1×。
Genozip 支持外部参考与“把引用到的参考片段存进归档”两种模式；后者更便于交付，但会增加
约 0.23 bytes/reference base。Genozip 还提供 `--bamass`，用已有 BAM 比对结果加速 FASTQ
压缩；官方自报可降低 40–60% CPU，并让归档再小 14%（Illumina WGS）/4%（MGI WGS）。

**结论**：只要允许参考，绝对最高压缩比路线就是有参路线。对 30× 人类短读 WGS，20–25×
原始、5–6× gzip 是已经被厂商和独立 benchmark 支撑的量级；但这不是 FQC 当前 fast 模式
能通过局部优化达到的。

### 3.3 无参路线：重排、伪基因组、上下文模型

#### SPRING

SPRING 用 min-hash 聚类把相似 read 排到一起，再做组装式序列编码（锚 read + 位置 + 编辑
脚本），质量值用 order-1 上下文 + 算术编码。论文自报 195 GB、25× NovaSeq 人类 WGS 压到
<7 GB，约 28× 原始【论文自报】；但 2025 独立 benchmark 在 82 个 GIAB NovaSeq 双端样本上
只有 3.79× vs gzip【独立 benchmark】。差异来自数据、双端处理、质量分布和比较口径，不能把
28× 当作通用数字。

SPRING 的“无损”也应按字段级理解：独立研究对其非 ACGTN IUPAC、`+` 行 metadata 和顺序
保留有争议；本文不直接称其为逐字节无损。

#### PgRC2

PgRC2 对高质量 read 构造近似最短公共超串（伪基因组），把 read 编码为伪基因组上的位置 +
差异；低质量 read 和含 N read 另建伪基因组。它主要压缩 **DNA 流**，论文的 ratio 单位是
bits per base；PgRC2 论文明确把“处理质量值和 header 的完整 FASTQ 压缩器”列为后续工作。
因此 PgRC2 的 bpb 不能直接与完整 FASTQ 归档比横比【论文自报】。

#### Mstcom

Mstcom 用 Hamming-shifting graph + 最小生成森林，把每条 read 编码为父 read 的差异；
它有 order-preserving 模式，通过存父 read ID/排列恢复原顺序。论文自报相对当时最佳方法
平均再小约 11%，相对 SPRING 平均约 22%；代价是内存和运行时间高，且在部分数据集上不稳定
【论文自报】。它是“全局相似性 + 差异编码”路线中最接近本方案 `max-ref-free` 的参考。

#### FQSqueezer

FQSqueezer 用 k-mer 字典/PPM 类上下文模型压缩序列，质量值支持 96/8/4/2/none 多种字母表；
其中 8/4/2/none 是有损档，不能与无损结果混比。它通常比 PgRC 再小约 10%（论文口径），但压缩和
解压都明显更慢，内存需求大【论文自报】。

#### fqxv

fqxv 是较新的工程化实现，结合重排、contig/差异编码和上下文模型。其 benchmark 文档自报：
NovaSeq 分箱质量数据上 `--order shuffle` 24.0×、`--max`（保序、存 permutation）20.4×，
SPRING 22.0×；GAIIx 全质量数据上分别为 11.97×、10.48×、10.32×【项目自报】。不同数据/
版本会出现更高或更低数字，不能把单一 29× 当作通用结论。它的 `--order shuffle` 会重编号，
属于“序列+质量集合无损、原始名字/顺序不保留”的交换；本文默认不采用。

#### FastqCA（2026）

FastqCA 用二维空间冗余/细胞自动机预测序列和质量。其 2026 年论文在 10 个数据集上报出
lossy 模式 13.46–18.48×，lossless 模式最高约 11.05×；lossy 数字使用 Q4 质量量化，不能
混入无损结论【论文自报】。它说明“跨 read × 跨位置”的二维建模仍有空间，但没有改变
“有参 > 无参、全局 > 流式”的基本格局。

### 3.4 长读路线

CoLoRd 用 overlap graph + anchor + 差异脚本压缩长读 DNA，质量流可用有损量化；论文自报
HiFi HG002 30× 从 219 GB 到 10 GB，约 22×，但其中包含质量有损处理【论文自报】。
NanoSpring 用近似组装/consensus 压缩 Nanopore 序列，论文只压缩序列，不压缩 ID/质量；
近期高质量数据可到 0.35–0.65 bits/base，相对 gzip 约 3–6×【论文自报】。
这两条路线的“高倍率”不能直接与完整 FASTQ 无损归档比横比；本文首版 max-ratio 以短读为主，
长读作为独立扩展，不把长读有损数字混入短读目标。

### 3.5 结论：最高压缩比到底是谁

| 场景 | 最高压缩比路线 | 代表 | 量级 | 主要代价 |
|---|---|---|---:|---|
| 人类短读，有参考 | 参考映射 + 差异 + 质量上下文 | ORA / Genozip | 20–25× 原始；5–6× gzip | 参考、mapper、参考版本绑定 |
| 短读，无参考，允许全局重排 | 伪基因组/重叠图 + 质量上下文 | PgRC2 / Mstcom / FQSqueezer / SPRING / fqxv | 10–18× 原始；分箱 NovaSeq 可到 20×+ | 两趟、全局内存、慢、实现复杂 |
| 短读，无参考，要求流式/低内存 | 2-bit + 通用熵编码 | 当前 FQC / DSRC 类 | 3–5× 原始 | 吃不到跨 read 和质量上下文 |
| 长读，允许质量有损 | overlap graph + 质量量化 | CoLoRd / FastqCA | 可达 20×+ | 有损、慢、复杂 |
| 长读，仅序列无损 | 近似组装/consensus | NanoSpring | 0.35–0.65 bpb（仅序列） | 不含质量/ID，不能直接比完整 FASTQ |

**因此，若目标是绝对最高压缩比：优先做 `max-ref`；若必须无参：做 `max-ref-free`。
两者都不应塞进现有 fast 帧流水线。**

## 4. 当前 FQC 为什么只有约 4×

### 4.1 本地真实样本

数据：Hartwig Medical 公开的 100k HiSeq 小样本 R1：

`https://raw.githubusercontent.com/hartwigmedical/testdata/master/100k_reads_hiseq/TESTX/TESTX_H7YRLADXX_S1_L001_R1_001.fastq.gz`

本地文件：

| 文件 | 字节数 | SHA-256 |
|---|---:|---|
| `hiseq_R1.fastq` | 6,538,676 | `de63a088f1a748153e0a42814dbb4e7dd8fb95a01fb2b2bafd51a2e975075058` |
| `hiseq_R1.fastq.gz` | 1,957,197 | `5c90a41c0fb951013fb7dc876ccbbb4a4dc9ed0526476f3d7c2086f0b9b18198` |
| `hiseq.fqc`（当前 clang-debug） | 1,634,437 | `19bdd931f2332376209c512d6c18228058949d8239131e8b626c9be528236b1c` |

样本统计：25,000 reads、101 bp、38 个质量符号、平均 Q37.26、12,391 个 N。
当前 FQC 压缩比 **4.0006× 原始**，相对 gzip **1.197×**。

### 4.2 归档体积拆解（4 MiB 帧目标）

| 流 | 压缩后 | 占归档 | 说明 |
|---|---:|---:|---|
| 质量值 | 0.790 MiB | ~50.7% | 原始 ASCII + zstd，零上下文建模 |
| 序列 | 0.648 MiB | ~41.6% | 2-bit 打包后 zstd 几乎不再缩小 |
| ID | 0.117 MiB | ~7.5% | 前缀重复已有 zstd 收益 |
| 帧/校验等 | 很小 | <0.3% | 不是杠杆 |

> 口径说明：上表是 **4 MiB 帧目标**的流级探针，不是 64 MiB 归档的逐字节拆解。实际归档
> （§11.3 的 64 MiB 命令）为 1,634,437 B；同一命令改 `--frame-mib 4` 为 1,635,111 B
> （+674 B，约 +0.04%）。这支持“帧大小不是压缩比杠杆”的结论，但不要把探针分项直接当成
> 归档字节账本。

关键事实：

- 序列 2-bit 打包后 zstd 只有约 1.07× 的额外收益；对随机/低覆盖短读，2-bit 已接近下限。
- 质量流是最大的一块，而且当前完全没有条件熵建模。
- ID 做了 front coding + comment 字典后，ID 流最多再省约 17%，对总归档只有约 1–2%。
- 1/2/4/8 MiB 帧目标对压缩比几乎没有影响；帧大小不是压缩比杠杆。

### 4.3 结构差距

当前 FQC 属于 DSRC 象限：列式分离 + 通用熵编码 + 帧独立 + 流式。它刻意放弃了两件最值钱的事：

1. **跨 read 序列冗余**：深覆盖 WGS 中大量 read 来自同一基因组区域，但流式 zstd 的窗口看不到
   全局相似性；只有重排/伪基因组/参考才能吃到。
2. **质量值的条件熵**：质量值沿 read 位置、前序质量、read2 标志强相关；zstd 只能吃表层重复，
   吃不到上下文条件分布。

因此，当前 4× 与 20× 的差距不是“调参”，而是“算法结构”的差距。

## 5. 方案总览

### 5.1 两条 profile，一个独立引擎

```text
                    ┌─ max-ref:      参考/PAF/BAM sidecar → 位置+方向+差异 ─┐
FASTQ bytes → parse ─┤                                                     ├→ 独立容器
                    └─ max-ref-free: minimizer/重叠 → 伪基因组 → 位置+差异 ─┘
                                      │
                                      ├─ 质量值: fqzcomp/rANS/zstd 自适应
                                      ├─ ID: token + 数字 delta + comment 字典
                                      └─ metadata: + 行、换行、异常字节
```

共同原则：

1. **独立格式族与独立 CLI 入口**，不复用 `fqc-sequential/v2` 的帧语义。
2. **全局分析阶段显式存在**；内存预算在编码前计算，超限则 spill 或拒绝，不允许“先跑起来再说”。
3. **原始 read 顺序保留**；不把 permutation 当作默认代价。
4. **每块自适应 codec**；本地样本中 fqzcomp 在 HiSeq 与 NovaSeq 分箱质量上都赢 zstd19，
   但仍保留 zstd 回退，避免其它仪器/文库上反转。
5. **失败关闭**：参考摘要不匹配、块长度越界、校验和不符、未知 codec 一律报错，不产生半成品。
6. **优先复用成熟实现**：htscodecs 的 fqzcomp/rANS、zstd；不自研 range coder。

### 5.2 为什么保留原始顺序也能吃跨 read 冗余

这是本方案最重要的结构判断：

- 传统重排路线先把 read 按相似性排序，再顺序编码，因此解压时需要 permutation 还原原序。
- 伪基因组路线可以先把所有 read 组装成一组全局 contig，然后**按原始顺序**逐条写
  “contig id + offset + orientation + mismatch/clip”。read 的写出顺序与原文件一致，
  但每条 read 的序列表示引用全局 contig。
- 这样不需要 permutation；contig 表是全局的，read 记录是原始顺序的。ID 的数字 delta 也
  因此仍然沿原顺序工作，不会被重排破坏。

这是一个设计假设，不是已证明的结果：它可能损失“相似 read 的局部质量模型”收益，但省掉
permutation 和顺序恢复逻辑。Phase 3 必须先量化这个 trade-off，再决定是否提供可选的
`--reorder` 变体。

## 6. `max-ref`：有参最高压缩比

### 6.1 输入模式

当前实验原型已实现第 0 种；其余是正式 `max-ref` 的计划：

0. **`--paf` + `--reference`（已实现，`fqc_ref_archive`）**：FASTQ + external reference
   FASTA + PAF sidecar。PAF 只用于压缩，解压只依赖 reference；当前接受 `M/=/X/I/D`
   CIGAR 与 `queryStart/queryEnd` 表达的左右 clip，`S/H/N/P` 或坐标不一致时回退 literal。
1. **`--bam-sidecar`（未实现）**：FASTQ + 已排序 BAM/CRAM。BAM 只作为比对 sidecar，不作为
   唯一输入；FASTQ 仍提供原始字节、顺序、`+` 行。要求 BAM 按 read name 可连接
   （name-sorted 或带索引），否则先外部排序/建索引，不静默猜顺序。
2. **`--reference` + FASTQ（未实现）**：内置轻量 seed-and-extend mapper（minimizer 索引 +
   参考窗口），只追求“足够好的压缩对齐”，不追求最优比对。
3. **`--reference` + `--embed-reference`（未实现）**：把实际引用到的参考片段存入归档，
   便于交付；归档体积会计入参考开销，报告中必须单列。

第一版不建议直接支持“只给 BAM、不给 FASTQ”：BAM 不保留原始 `+` 行和原始顺序，无法满足
本文的逐字节无损目标。若未来支持，必须明确是“FASTQ 字段重建”而不是“原始文件字节重建”。

### 6.2 参考索引

- 正式方案：对参考做 canonical minimizer 索引，记录 contig id、位置、方向；索引可持久化
  缓存；归档只存参考摘要、contig 名称/长度和索引参数，不存外部参考本体。
- **当前实验原型不建索引**：比对由外部 PAF 提供；归档只存 contig 表与 XXH64 摘要。
- 解压时必须提供同一参考；摘要不匹配立即失败，不回退到“近似参考”。
- 参考中的 N/简并碱基作为普通符号处理；read 映射失败则回退到 2-bit/literal。
- 对重复区域，允许多个候选位置；选择“差异最少 + 位置编码最便宜”的候选，必要时存候选
  列表或直接回退。第一版可以只取最佳候选，Phase 2 的后续迭代再优化。

### 6.3 read 编码类别

| 类别 | 存储 | 适用 | 当前实验原型 |
|---|---|---|---|
| `PERFECT` | contig id + offset + orientation + length | 完全匹配 | 支持（作为 mismatch=0 的 mapped） |
| `MISMATCH` | 上述 + mismatch 位置 delta + 2-bit alt | 少量替换 | 支持（`--max-mismatches` 内） |
| `CLIPPED` | 上述 + query 范围 + 左右 clip 序列（base code） | 末端不匹配/软剪切 | 支持（PAF queryStart/queryEnd） |
| `INDEL` | 上述 + CIGAR I/D；I 存 base code，D 不存序列 | 短 indel | 支持（M/=/X/I/D） |
| `LITERAL` | 2-bit + 异常列表 | 未映射、低质量、复杂 CIGAR | 支持（当前原样序列 + zstd） |

Phase 2.2 已把 CIGAR 显式编码落进 `fqc-max-ref/v1`：`M/=/X` 中的实际替换由参考与 read
重新计算，`I` 存 query 多出的 base code，`D` 只消费参考；query 范围短于整条 read 时存
原始方向左右 clip。`S/H/N/P`、空/零长度/超过 256 个操作或坐标消费量不一致时仍回退
literal。负链先在 reverse-complement 后的 aligned query 上执行 CIGAR，解码再反向还原。

### 6.4 质量、ID、metadata

- 质量值：按块使用 fqzcomp 风格上下文模型；可利用 read2 标志、位置、前序质量、running
  delta。N 碱基的质量值单独分组，避免污染主模型。
- ID：token 化，数字 token 按前一条 read 做 delta，非数字 token 走 zstd；comment 做字典。
- `+` 行：空、等于 header、任意 literal 三态；literal 进字典。
- 换行：全局 LF/CRLF 模式 + 每记录例外；混合行尾不得静默规范化。

### 6.5 解码

1. 校验容器头、参考摘要、块索引；
2. 读取 contig 表并重建参考片段（外部参考则从用户提供的参考取）；
3. 逐条按原始顺序解码 read：位置/方向/差异/clip 或 literal；
4. 解码质量、ID、`+` 行、换行；
5. 逐块校验 XXH64，最后校验全局计数与滚动校验和；
6. 任何参考缺失/摘要不符/越界都 fail closed。

当前 `fqc-max-ref/v1` 原型做第 1、2、3（PAF M/=/X/I/D + 左右 clip）、4（ID/quality，
canonical FASTQ）、5；`+` 行/行尾/空 read 的完整保真仍待最终格式。解码不需要 PAF，
只需要同一 external reference。

### 6.6 目标、原型结果与代价

- 目标【设计目标】：30× 人类短读 WGS，原始 FASTQ 约 235 GB 量级，归档 ≤11–12 GB
  （约 20–21× 原始、5× gzip）；最终追平 ORA/Genozip 的 5–6× gzip 量级。
- 原型结果【本地实验】：5 Mb 随机参考 + wgsim 3× 双端 150 bp，R1 17,423,235 B →
  792,217 B（21.993× raw），R2 → 796,633 B（21.871× raw）；mapped 49,998/50,000，
  literal 2/0，clipped 51/44，indel 753/745；`verify` + `cmp` 通过。详细命令/SHA/字节账见
  [Phase 2.2 CIGAR 基线](max-ratio-baselines/2026-11-15-ref-cigar.md)；Phase 2 旧基线见
  [ref-container](max-ratio-baselines/2026-11-15-ref-container.md)。
- 真实 WGS 小切片【本地实验】：`SRR14724532` R1 头部 298,974 reads / 45,145,074 bases，
  完整 hg38 参考、minimap2 2.31 primary PAF；归档 **4,547,935 B（21.974× raw）**，
  mapped 296,343（99.1%）、literal 2,631、clipped 11,220、indel 10,181；quality
  2,181,008 B、alignment 2,352,120 B、ID 3,701 B；`verify` + `cmp` 通过，重复压缩
  逐字节一致。详见
  [真实 WGS 基线](max-ratio-baselines/2026-11-15-real-wgs-srr14724532.md)。
- **原型结果不是 30× 人类 WGS 结论**：参考不计入、恒定 Q23、随机参考、3× 覆盖、
  只支持 PAF 的 M/=/X/I/D（不支持 S/H/N/P）；真实数据的 ID/质量/复杂 CIGAR/重复区域
  会显著改变比例。
- 代价：需要参考、mapper/索引、参考版本绑定；压缩与解压都比 fast 模式重。
- 不计入归档的外部参考不能算“免费”；报告必须写“external reference, not counted”。

## 7. `max-ref-free`：无参最高压缩比

### 7.1 两趟结构

```text
Pass 1（全局分析）
  parse → 选高质量 read → canonical minimizer → 分桶 → 桶内重叠/consensus
        → 生成 contig 表 + read→contig 候选

Pass 2（按原始顺序编码）
  逐条 read（原始顺序）→ PERFECT / MISMATCH / CLIPPED / LITERAL
  → 质量流（块内上下文编码）
  → ID/comment/plus-line/换行
  → 块校验 + footer
```

Pass 1 可以按桶流式处理，不要求把所有 read 同时放内存；Pass 2 顺序读输入，按 contig 表编码。
对不可 seek 的 stdin/gzip，Pass 1 需要临时 spill 或要求可重复读取；第一版可以只支持普通文件
与可 seek 的 gzip，stdin 明确报“不支持 max-ref-free”，不静默降级。

### 7.2 伪基因组构造

1. 计算每条 read 的 canonical minimizer（例如 k=15–21，按 read 长度和错误率调）。
2. 按 minimizer 分桶；桶内做轻量重叠检测，不要求完整 assembler。
3. 先放置高质量 read（质量均值高、N 少），形成 consensus contig；后续 read 通过 seed+extend
   放置，更新 consensus。
4. 含 N/低质量 read 单独成桶或走 literal，不强行污染主 contig。
5. 对反向互补做 canonical 方向；每条 read 存 orientation bit。
6. contig 用 zstd level 19（后续可评估 LZMA）压缩；contig 表按长度/名称/摘要索引。

这不是“把 reads 拼成完整基因组”，而是“构造一组足够好的压缩参考”。允许 contig 碎片化；
碎片化只影响压缩比，不影响正确性。

### 7.3 read 编码

与 `max-ref` 相同的四类，只是参考来自伪基因组而非外部参考。差异脚本：

- mismatch 位置按 delta 编码，alt 用 2-bit；
- clip 序列用 2-bit，异常符号用异常列表；
- 无法放置的 read 走 literal（2-bit + 异常列表）；
- 每条 read 存长度、contig id、offset、orientation、类别、差异计数。

### 7.4 保序、重编号与 permutation

默认：**保序，不存 permutation**。read 记录按原始顺序写出，contig 表是全局的。
可选实验：

- `--reorder`：先重排再编码，存 permutation 以还原原序；用于测量“局部聚类”是否真的带来
  额外收益。
- `--renumber`：不保留原始名字/顺序，只保留 sequence+quality 集合；这是 lossy 语义，
  不进入默认路径，也不进入“无损”验收。

fqxv 的公开 benchmark 显示保序 `--max` 与 shuffle 之间有约 15–20% 的差距，但其中包含
permutation 和重编号的交换；本方案先验证“全局 contig + 原序引用”能否在不存 permutation
的情况下接近这个差距。

### 7.5 目标与代价

- 目标【设计目标】：全质量短读 ≥10× 原始；NovaSeq 分箱质量 ≥18–20× 原始；
  深覆盖 WGS 争取接近 SPRING/PgRC2/Mstcom 的 10–18× 区间。
- 代价：两趟、全局 contig 表、桶内重叠计算、较大内存/磁盘 spill；比 `max-ref` 慢。
- 首版只承诺短读（≤300 bp）。长读需要 indel/剪辑和 overlap graph 扩展，单独评估，
  不把 CoLoRd/NanoSpring 的有损数字混入短读目标。

## 8. 质量值、ID 与 metadata 的共同设计

### 8.1 质量值：上下文模型，不搞朴素变换

本地真实 HiSeq 的探针说明：朴素 delta/transpose/6-bit 在真实数据上并不稳定：

| 质量变换（4 MiB 帧目标） | 压缩后 | 相对 raw 质量流 |
|---|---:|---:|
| raw + zstd | 0.790 MiB | 基线 |
| read-delta | 0.802 MiB | +1.5% |
| transpose | 0.894 MiB | +13.1% |
| transpose + read-delta | 1.124 MiB | +42.2% |
| transpose + position-delta | 0.796 MiB | +0.8% |
| transpose + xor-prev | 1.063 MiB | +34.4% |
| 6-bit 打包 | 0.908 MiB | +14.9% |

合成数据则会给出相反结论：60k × 150 bp、400 模板、1.5% 突变、质量随机游走的数据上，
raw 2.958 MiB → delta 2.417 MiB（约 −18.3%）。**合成数据会高估 delta/transpose 收益；
真实数据必须作为验收面。**

因此质量方案是：

- 复用 htscodecs 的 `fqzcomp_qual`（BSD-3-Clause；`v1.6.7` 已本地验证）；
- 按块训练/更新上下文模型，不跨块保留全局稠密表；
- 每块优先试 `fqzcomp` 策略 0 与 1，取较小者；zstd 只作为失败/无收益时的回退；
- 块目标从 **4M bases** 起步，按 bases 而不是 read 条数切块；HiSeq 1k reads/块的
  fqzcomp 产物比整文件差 30.1%，小块会因模型 warmup 明显变差；
- 本地三个样本中 fqzcomp 均赢 zstd19：HiSeq 全质量 −17.7%，NovaSeq RNA-Seq 分箱
  −22.9%，WGS 分箱 −23.2%；仍保留 zstd 回退，但不再假设分箱质量一定由 zstd 获胜；
- N 碱基质量单独分组；read2 用 flag 区分（本轮未测 read2）；
- 不把 `--optimise`、Q4/Q8 量化等有损模式混入默认路径。

完整实验、分块曲线与归档影响估算见
[docs/max-ratio-baselines/2026-11-15-quality-codecs.md](max-ratio-baselines/2026-11-15-quality-codecs.md)。

### 8.2 ID：token + 数字 delta + comment 字典

真实 HiSeq 上 ID 只占归档约 7.5%，但仍有低成本收益：

| ID 变换（4 MiB 帧目标） | 压缩后 | 相对 raw ID 流 |
|---|---:|---:|
| raw + zstd | 0.117 MiB | 基线 |
| front coding | 0.113 MiB | 约 −3% |
| front + comment 字典 | 0.097 MiB | 约 −17% |

设计：

- 把 ID 切成“非数字 token / 数字 token”交替序列；
- 数字 token 按前一条 read 做 delta，非数字 token 按列/字典压缩；
- comment 做块内字典；
- 全字节保留，不做空白规范化；
- 对非常规 ID（UUID、无数字、超长）自动回退到原始字节 + zstd。

### 8.3 `+` 行与换行

当前 v2 丢弃 `+` 行；max-ratio 要扩大无损边界，必须处理：

- `+` 行为空：只存一个 flag；
- `+` 行等于 header（含 `@` 或不含）：存引用；
- 其他 literal：块内字典；
- 换行：全局 LF/CRLF + 每记录例外；混合行尾要么完整还原，要么在解析阶段明确拒绝；
- 空 read、零长度 quality、大小写/IUPAC 原样保留。

### 8.4 校验和

- 每块：XXH64 覆盖未压缩的逻辑流与压缩载荷；
- 全局：滚动 XXH64 覆盖块校验和与计数；
- 参考：SHA-256 摘要（参考身份不能只用 XXH64）；
- 这些是完整性检测，不是密码学认证。

## 9. 容器与格式草案（TBD，不是规范）

建议使用独立 magic/family，例如 `fqc-max/v1`（名字待定），不复用 `fqc-sequential/v2`。
草案流：

```text
header
  magic/version/profile/flags
  block size, codec table
  reference digest / contig count
  newline policy
global tables
  contig table (id, name, length, compressed bases, checksum)
  comment dictionary / plus-line dictionary (optional)
blocks
  read descriptors (sequence class + contig/offset/orientation + diffs)
  quality payload (codec id + model params)
  names payload
  plus-line payload
  block checksum
footer
  read count / base count / block count
  global checksum
  reference digest repeat
```

格式设计约束：

- 所有长度/计数使用 varint，解码时做溢出/上限检查；
- 未知 profile/codec/version 明确拒绝；
- 参考摘要不匹配、contig 越界、差异位置越界、质量长度不匹配均 fail closed；
- 块大小可配置，但解码不依赖编码端内存；
- 不在第一版承诺随机访问；块索引可以后续加，但不能破坏顺序解码。

实现前必须在 `openspec/changes/` 写格式契约，并通过 ADR 复审；本文不冻结任何字节。

## 10. 分阶段实施

实施顺序按“绝对压缩比优先”排列：先做有参 `max-ref`，再做无参 `max-ref-free`。这不是
对无参路线的否定，而是因为用户目标明确要求先找到最高压缩比，再考虑速度与通用性。

### Phase 0：压缩比 benchmark 底座（已完成测量面）

- 已实现 `tools/ratio_probe`（不安装、不进入主 CLI）：按流统计原始字节、行尾、质量分布、
  packed sequence 与 gzip/zstd 基线；测试覆盖 CRLF、空 read、`+` 行和无最终换行。
- 已复核本地 HiSeq 的当前 FQC 归档、SHA-256 与 64/4 MiB 帧差异（见 §12.6）。
- 已建立数据集 manifest：HiSeq 全质量、NovaSeq 分箱质量头部子集、WGS 分箱质量头部子集
  与完整 35× run 元数据；见 [datasets.md](max-ratio-baselines/datasets.md)。WGS 完整
  run 未下载，完整 run 的序列冗余/覆盖分布仍待补。
- 不写生产格式，不改 CLI。
- 验收：同一数据集重复运行字节数稳定；报告能区分 raw/gz/bpb 口径。

### Phase 1：质量 + ID + metadata（质量 codec 门禁与实验容器已实现）

- 已实现实验容器 `fqc-max/v1` 与 `max-compress` / `max-decompress` / `max-verify`：
  独立 magic/头部/footer，ID 与 2-bit packed sequence 复用 v2 逻辑流，质量流按块在
  fqzcomp（策略 0/1 自适应）与 zstd19 之间取较小者。
- 本地质量流结果：HiSeq 558,189 B vs zstd19 678,308 B（−17.7%）；NovaSeq RNA-Seq
  头部 2,159,788 B vs 2,802,326 B（−22.9%）；WGS 头部 2,168,649 B vs 2,823,235 B
  （−23.2%）。按当前归档只替换质量流的估算为 −15.5% 至 −16.6%。
- 实际容器结果（ID/sequence 也用 zstd19）：HiSeq 1,285,285 B（相对 v2 −21.36%，
  5.087× raw）；NovaSeq 头部 12,280,644 B（−24.87%，8.165× raw）；WGS 头部
  12,828,169 B（−23.69%，7.790× raw）。三个归档 `max-verify` 与 `cmp` 均通过。
- 目标：真实 HiSeq 与 NovaSeq 上归档相对当前 FQC 至少 −15%；不设速度门禁。**已通过。**
- 验收：当前容器只对 canonical LF 输入做逐字节 round-trip；CRLF/`+` 行内容/空 read
  的逐字节保真仍未实现，不能把本阶段当作最终 max-ratio。
- 停止条件：如果质量流在真实数据上不能稳定获得 ≥10% 自身缩减，暂停后续序列重排投入，
  先修正模型/数据面。**本轮三个样本均通过该停止条件。**

### Phase 2：`max-ref`（PAF 原型已实现；正式验收未完成）

- 已完成：开发工具 `fqc_ref_archive` + `fqc-max-ref/v1`，支持
  `FASTQ + PAF + external reference`；mapped 正/负链、mismatch delta/alt、CIGAR
  M/=/X/I/D、左右 clip、literal 回退；quality fqzcomp/zstd 自适应；块级/全局 checksum；
  参考摘要/contig 表校验。
- Phase 2.1 已加入自适应 ID front coding（`idCodec=2`）：每块在 raw 与 front-coded
  两种逻辑流各自 zstd19 后取较小者；真实 WGS/NovaSeq 头部 ID 探针约 −99%，随机
  wgsim ID 自动回退 raw。
  结构化 ID 合成归档 331,553 B（53.385× raw），但这是机制验证，不是真实 WGS 结论。
- Phase 2.2 CIGAR：R1 827,688 → 792,217 B（21.993× raw），R2 827,546 → 796,633 B
  （21.871× raw）；mapped 49,998/50,000，literal 2/0，clipped 51/44，indel 753/745；
  结构化 ID R1 331,553 → 296,082 B（59.781× raw）；`verify` + `cmp` 通过，重复压缩
  SHA 一致。
- 真实 WGS R1 头部：完整 hg38 + 298,974 reads，归档 4,547,935 B（21.974× raw），
  mapped 296,343、literal 2,631；`verify` + `cmp` 通过，重复压缩 SHA 一致。修复了
  soft-masked 参考大小写导致的误判（修复前仅 109,476 mapped、9.741×）。
- 未完成：内置 mapper、BAM sidecar、S/H/N/P/长读 spliced alignment、参考嵌入、SHA-256、
  完整 30× WGS、paired-end 联合、数字 ID token/delta、原子/有界执行。
- 目标：30× 人类短读 WGS ≥20× 原始、≥5× gzip；参考摘要/解压路径严格校验。
- 验收：与 ORA/Genozip 同数据同口径对比（若可获得）；参考不计入/计入两套数字都报告。
- 实现顺序调整：先用 PAF 验证结构，再决定 BAM sidecar 或内置 mapper；当前原型不承诺
  BAM-only。
- 停止条件：如果 PAF/参考分发成本不可接受，或真实 WGS 上 mapped/质量/ID 收益不足以
  接近目标，只保留 external reference 实验工具，不把它包装成产品；若参考依赖仍不可接受，
  停止 max-ref，转 Phase 3 无参路线。

### Phase 3：`max-ref-free`（无参通用路线）

- 实现 minimizer 分桶 + 轻量 consensus + 原序引用；先短读。
- 目标：全质量短读 ≥10× 原始；分箱 NovaSeq ≥18–20× 原始。
- 验收：深覆盖 WGS 上 ratio 超过 SPRING 的 3.79× vs gzip 量级；逐字节 round-trip；
  maxRSS 在预算内；spill 路径可恢复。
- 停止条件：如果 16 GiB 预算下无法稳定超过 8× 原始，或 contig 构造的复杂度导致无法在
  可接受时间内完成，保留 `max-ref` 为唯一推荐路线，不继续堆全局重排。

### Phase 4：速度与内存优化

- 只有 Phase 1–3 的压缩比达标后才开始。
- 方向：块级并行、参考索引复用、质量模型状态复用、zstd 输出分配复用、SIMD rANS；
- 不以速度回退换取未经验证的压缩比；所有优化都要有 A/B 同窗口证据。

### 门禁

- 实现前：`openspec/changes/` 格式契约 + ADR 复审 + 本设计文档更新。
- 每个 Phase：真实数据 round-trip、畸形输入、内存上限、压缩比报告四件套。
- 不允许先写代码再补格式；不允许把全局状态塞进现有 fast 帧流水线。

## 11. 压缩比验收协议

### 11.1 必报指标

对每个数据集、每个工具：

- 原始 FASTQ 字节、`fastq.gz` 字节、归档字节；
- `R_raw`、`R_gz`；
- 序列/质量/ID/metadata 分流字节与 bits/base（如工具允许）；
- 压缩/解压 wall time、maxRSS、线程数；
- 是否保序、是否保名、是否保 `+` 行、是否逐字节 `cmp`；
- 参考模式：external / embedded / none；参考大小是否计入；
- 质量是否有损；若有损，量化档位与误差。

### 11.2 数据集

首轮建议：

1. 本地 HiSeq 小样本（本文 §4.1，SHA 已记录）：质量/ID 探针，不代表深 WGS。
2. NovaSeq 分箱质量短读：使用 `DRR174812` R1 头部 21,110,784 B 子集（303,610 reads）
   验证质量 codec；注意它是 RNA-Seq，不是 WGS。
3. 深覆盖人类 WGS 短读：`SRR14724532`（GIAB/NIST，NovaSeq，约 35×）已固定头部
   21,110,784 B 子集（298,974 reads）与完整 run manifest；完整 35× run 未下载，
   Phase 2/3 前必须建立完整/分片快照。
4. **Phase 2 合成机制验证**：5 Mb 随机参考 + wgsim 3× 双端 150 bp（固定 seed 20261115、
   PAF、SHA 均在 [Phase 2 基线](max-ratio-baselines/2026-11-15-ref-container.md)）；
   只验证 reference/PAF/编码/校验链路，不作为真实 WGS 压缩比。
5. ONT/PacBio 长读（可选）：只用于长读扩展评估，不与短读目标混比。

完整 accession、URL、MD5、本地 SHA 与快照范围见
[datasets.md](max-ratio-baselines/datasets.md)。

### 11.3 命令与校验

当前 fast 模式的复现命令（仅用于基线，不是 max-ratio）。2026-11-14 用该命令重跑，
输出 1,634,437 B，SHA-256 与 §4.1 表中一致；`--frame-mib 4` 重跑输出 1,635,111 B：

```bash
build/clang-debug/src/fqc compress \
  -i /tmp/fqc-exp/hiseq_R1.fastq \
  -o /tmp/fqc-exp/hiseq.fqc \
  --frame-mib 64 --parse-workers 4 --quality-level 1 --force
sha256sum /tmp/fqc-exp/hiseq_R1.fastq /tmp/fqc-exp/hiseq_R1.fastq.gz /tmp/fqc-exp/hiseq.fqc
```

max-ratio 实现后，每个归档必须：

```bash
fqc decompress -i archive -o restored.fastq
cmp original.fastq restored.fastq
```

当前 Phase 2.2 原型的复现命令：

```bash
BIN=./build/clang-debug/tools/fqc_ref_archive
"$BIN" compress -i reads.fastq --paf reads.paf -r reference.fa \
  -o reads.fqmr --block-bases 4000000 --max-mismatches 16 -f
"$BIN" verify -i reads.fqmr -r reference.fa
"$BIN" decompress -i reads.fqmr -r reference.fa -o restored.fastq -f
cmp reads.fastq restored.fastq
```

并对 `+` 行、CRLF、IUPAC、空 read、paired-end 分别做 fixture；不能只测“能解压”。
Phase 2.2 原型当前只覆盖 canonical LF FASTQ；上述 fixture 仍是最终格式的门禁。

### 11.4 与外部工具对比的公平性

- 同数据、同双端处理、同线程数、同参考；
- 明确 Genozip/ORA 是否使用参考、参考是否随归档存储；
- 明确 SPRING 是否重排、是否保名、是否能重建 MD5；
- 明确 FQSqueezer/FastqCA/CoLoRd 是否使用有损质量；
- 不把 DNA-only bpb 与完整 FASTQ 归档比放在同一列；
- 不引用厂商“最高可达”数字作为中位数。

## 12. 本地实验记录

### 12.1 环境

- 主机：WSL2，Ryzen 7 5800H（与仓库既有性能记录同一环境）。
- 当前二进制：`build/clang-debug/src/fqc`（2026-09-21 构建；功能探针，不是性能 benchmark）。
- 历史探针：Python 3 + `zstandard`，zstd level 1，按 1/2/4/8 MiB 帧目标切块；每块独立
  压缩后求和。脚本没有提交到仓库，因此这些数字只作方向性证据。
- 正式探针（2026-11-15）：`build/clang-debug/tools/fqc_ratio_probe`，源码见
  [tools/ratio_probe](../tools/ratio_probe/README.md)，输出 `fqc-ratio-probe/v1` JSON；
  结果与命令见 [本地 HiSeq 基线](max-ratio-baselines/2026-11-15-hiseq.md)。
- 正式探针使用独立 byte-preserving scanner，不复用生产 `FastqParser`；其 gzip 基线是进程内
  zlib level 6，不是外部 `gzip -6` 文件。
- 历史数字是“流级探针”，不是 FQC 正式归档的逐字节复现；用于判断方向，不作为发布性能数字。

### 12.2 真实 HiSeq 结果

见 §4.1 与 §8.1。补充：

- 序列：packed 0.648 MiB，packed + xor-prev 0.670 MiB（更差）；说明“前一条 read XOR”在
  这个样本上没有收益，不能替代全局伪基因组。
- ID：front 0.113 MiB，front + comment 字典 0.097 MiB；对总归档约 1–2%。
- 帧目标 1/2/4/8 MiB 的结论一致；帧大小不是杠杆。

### 12.3 合成数据对照

- 60k × 150 bp，400 模板，1.5% 突变，0.05% N，质量随机游走 Q20–41。
- raw 质量 2.958 MiB → delta 2.417 MiB（约 −18.3%）。
- transpose 4.123 MiB、6-bit 3.891 MiB，均明显更差。
- 结论：合成数据会高估 delta/transpose；任何质量变换必须用真实数据验收。

### 12.4 zstd 分配微基准（速度线索，未集成）

64 MiB 混合数据、zstd level 1 的独立微基准在同一二进制上跨次运行可见约 268–349 MiB/s 的
波动；`fresh vector(zero-init)`、`fresh new[](uninit)`、`reused vector` 的相对顺序并不稳定，
不能把单次表当成产品结论。另一次 one-shot vs 复用 `ZSTD_CCtx` 的运行是 295.9 vs 326.9 MiB/s
（约 10.5%），但同样受环境和数据影响。

这说明输出分配/零初始化与 CCtx 复用可能是 Phase 4 的速度线索；本文不据此修改生产代码，
也不把微基准外推为端到端收益。

### 12.5 本地实验的未验证边界

- 25k reads 不足以代表 30× WGS 的跨 read 冗余；序列伪基因组的收益必须用深覆盖数据复测。
- 本地 NovaSeq 头部样本上 fqzcomp 仍赢 zstd19；但它是 RNA-Seq 头部子集，完整 run、
  WGS 与其它仪器上的反转风险仍未验证。
- 没有在本地复测 ORA/Genozip/SPRING/PgRC2/Mstcom；外部数字只按来源等级引用。
- Phase 1 实验容器、Phase 2 PAF 参考原型与真实 WGS R1 头部 + 完整 hg38 小切片已实现；
  完整 30× WGS、Phase 3（无参伪基因组）与所有 10×/20× 目标仍未实现/未验收。

### 12.6 Phase 0 ratio probe 结果（2026-11-15）

本地 HiSeq 小样本（25k reads、101 bp、2,525,000 bases、12,391 N）结果：

| 指标 | 字节/数值 |
|---|---:|
| raw FASTQ | 6,538,676 |
| zlib gzip level 6（探针） | 1,916,752 |
| zstd level 19 全文件（探针） | 1,617,921 |
| zstd level 19 ID/comment 流 | 89,729 |
| zstd level 19 quality 流 | 681,586 |
| FQC-like packed sequence | 724,785 |
| 当前 FQC（64 MiB 单帧） | 1,634,437 |

由此得到：

- 探针的 raw/gzip 为 3.411×、raw/zstd 为 4.041×；当前 FQC 为 4.0006× raw。
- zstd19 全文件比当前 FQC 小约 1.0%，但这是通用压缩基线，不是 FASTQ 专用结构；
  不能据此得出“zstd 足够”的结论。
- 质量流 zstd19 为 681,586 B（按总 bases 计 2.159 bpb），是 Phase 1 上下文编码的主要
  对照面；当前 fast 路径的 quality level 1 不是压缩比最优。
- 25k reads 不能代表深覆盖 WGS 的跨 read 冗余；WGS 头部质量已测，但完整 run 的序列
  冗余/覆盖分布仍未验证；Phase 2 已有合成与真实 R1 头部小切片，但不是完整 30×，
  Phase 3 未实现。

完整 JSON、命令、输入 SHA 与探针输出 SHA 见
[docs/max-ratio-baselines/2026-11-15-hiseq.md](max-ratio-baselines/2026-11-15-hiseq.md)。
探针实现说明见 [tools/ratio_probe/README.md](../tools/ratio_probe/README.md)。

### 12.7 质量 codec 探针（2026-11-15）

用 htscodecs `v1.6.7` fqzcomp 对 quality 流做独立实验；不写归档、不改生产 parser：

| 数据集 | quality 原始 | zstd19 | fqzcomp | 相对 zstd19 | 当前 FQC 质量流 zstd1 | 估算总归档缩减 |
|---|---:|---:|---:|---:|---:|---:|
| HiSeq 25k | 2,525,000 | 678,308 | 558,189（strat 1） | −17.7% | 828,713 | 1,634,437 → 1,363,913（−16.6%） |
| NovaSeq 头部 303,610 reads | 45,541,500 | 2,802,326 | 2,159,788（strat 0） | −22.9% | 4,827,256 | 16,344,935 → 13,677,467（−16.3%） |
| WGS 头部 298,974 reads | 45,145,074 | 2,823,235 | 2,168,649（adaptive 0/1） | −23.2% | 4,779,205 | 16,811,413 → 14,200,857（−15.5%） |

关键结论：

- 三个样本（HiSeq 全质量、NovaSeq RNA-Seq 分箱、WGS 分箱）都通过“质量流稳定缩减
  ≥10%”门禁；两个分箱质量样本上 fqzcomp 都没有输给 zstd，反而领先约 23%。
- strat 0/1 每块自适应足够；strat 2/3 在已测样本上都更差。
- 块大小必须按 bases 而不是 reads：HiSeq 1k reads/块比整文件差 30.1%；建议从 4M bases/块
  起步。
- 估算只替换质量流，未计入新容器/参数/索引开销，也未处理序列流；不能把 −15.5%～−16.6% 当作
  max-ratio 总压缩比，更不能据此宣称 18–20×。

完整数据、分块曲线、输入/输出 SHA、固定工具版本和未验证边界见
[docs/max-ratio-baselines/2026-11-15-quality-codecs.md](max-ratio-baselines/2026-11-15-quality-codecs.md)；
探针见 [tools/quality_probe/README.md](../tools/quality_probe/README.md)。

### 12.8 Phase 1 实验容器 `fqc-max/v1`（2026-11-15）

把质量 codec 从探针落成实际归档；ID/sequence 外层使用 zstd19，质量按块在 fqzcomp 与
zstd19 之间取较小者：

| 数据集 | raw FASTQ | 当前 v2 | `fqc-max/v1` | 相对 v2 | raw / max | `max-verify` + `cmp` |
|---|---:|---:|---:|---:|---:|---|
| HiSeq 25k R1 | 6,538,676 | 1,634,437 | 1,285,285 | −21.36% | 5.087× | 通过 |
| NovaSeq RNA-Seq 头部 | 100,272,700 | 16,344,935 | 12,280,644 | −24.87% | 8.165× | 通过 |
| WGS 头部 | 99,934,080 | 16,811,413 | 12,828,169 | −23.69% | 7.790× | 通过 |

注意：

- 相对 v2 的缩减**不是纯质量 codec 收益**；ID/sequence 的 zstd19 也贡献了体积差。质量流
  独立对照以 §12.7 为准。
- 容器复用生产 `FastqParser`，输出 canonical LF FASTQ；`+` 行内容、混合行尾、空 read 的
  原始字节不在保证范围内。最终 max-ratio 必须补 byte-preserving scanner。
- 三个输入/输出 SHA、复现命令与未验证边界见
  [docs/max-ratio-baselines/2026-11-15-max-container.md](max-ratio-baselines/2026-11-15-max-container.md)；
  格式契约见
  [openspec/changes/2026-11-15-max-ratio-phase1-container/design.md](../openspec/changes/2026-11-15-max-ratio-phase1-container/design.md)。

## 13. 经验、反模式与停止条件

### 13.1 v1 教训

v1 曾走 ABC/SCM/ReorderMap 路线，最终因“重排序、编码器状态与内存核算耦合错位，内存上限
无法强制执行”删除 27,835 行。max-ratio 必须：

- 独立引擎，不把全局状态塞进 fast 帧流水线；
- 先做内存预算与 spill，再做编码；
- 质量模型块内局部/有界，不复制每 worker 稠密表；
- 每个全局结构（contig 表、参考索引、permutation）都有显式所有权与生命周期；
- 设 kill criteria，避免沉没成本。

### 13.2 反模式

1. **继续调 zstd level**：只能改变几个百分点，不能补结构差距。
2. **质量 delta/transpose/6-bit 一把梭**：真实 HiSeq 上大多更差；合成数据会骗人。
3. **把 DNA-only bpb 当完整 FASTQ 比**：PgRC2/NanoSpring 的数字不能直接横比。
4. **把有损质量数字混入无损结论**：FQSqueezer 低字母表、CoLoRd、FastqCA lossy、
   Genozip `--optimise` 都要单列。
5. **把 SPRING 称为逐字节无损**：字段级/顺序/metadata 边界必须先验证。
6. **有参压缩不报参考开销**：外部参考不是免费；嵌入参考要计入归档。
7. **先写代码再补格式**：max-ratio 的容器/参考/permutation 契约必须先过 openspec。
8. **为并行而并行**：压缩比未达标前不做速度门禁；Phase 4 再用 A/B 数据决定。

### 13.3 停止条件

- Phase 1 质量流在真实数据上不能稳定缩减 ≥10%：暂停序列重排投入。**2026-11-15
  HiSeq + NovaSeq RNA-Seq 头部 + WGS 头部探针均通过；后续完整 run 仍需复核。**
- Phase 2 参考分发/摘要/解压依赖无法接受：只保留 external reference + PAF/BAM sidecar，
  不承诺“自包含归档”；若仍不可接受，停止 max-ref，转 Phase 3 无参路线。
- Phase 2 原型已在合成数据上通过 `verify`/`cmp` 与 21× raw 机制验证，并在真实 WGS
  R1 头部 + 完整 hg38 上得到 21.974×（参考/PAF 不计入）；**完整 30× WGS 与 paired-end
  尚未验收**。不把 R1 头部数字写成完整 run 产品结论，也不启动 Phase 4 速度优化。
- Phase 3 在 16 GiB 预算下无法稳定超过 8× 原始：保留 `max-ref` 为唯一推荐路线，不继续
  堆全局重排。
- 实现规模超过可维护边界（例如需要引入完整 assembler/aligner/多后端框架）：缩小到
  短读 + 单一后端，优先保证正确性和可验证性。

### 13.4 许可证边界

本文引用的是**算法思想**，不是把外部实现直接搬进本仓库。实现前必须把“算法参考”和“代码复用”
分开审查：

- **可直接评估复用**：htscodecs（rANS/质量/name codec，BSD/MIT 兼容，需逐文件核对）、
  zstd（BSD）、libbsc（Apache-2.0，可选后端）、fqxv（MIT/Apache-2.0，若未来需要 Rust 侧对照）。
- **只读算法参考、不要复制代码**：PgRC/PgRC2 与 Mstcom 是 GPL-3.0；CoLoRd 是 GPL-3.0；
  把它们的目标代码并入 MIT 项目会改变分发义务。FQSqueezer 的许可证在公开仓库中不清晰，
  SPRING 的发布条款含 research-use 限制；两者都只能作为论文/行为参考，除非单独取得许可。
- **NanoSpring** 为 MIT，但它只压序列，且自带第三方组件（如 libbsc/minimap2）需单独核对。
- 任何“复刻”都必须先确认许可证、专利/归属声明与构建依赖；不能用“论文公开”替代许可审查。

这条边界也是选择“自研轻量伪基因组 + htscodecs 质量模型”而不是直接 fork PgRC/Mstcom 的原因。

## 14. 开放决策

1. 格式族名与 magic：`fqc-max/v1`、`fqc-max-ref/v1` 是否长期保留，还是按 profile
   分两个 family；当前 Phase 1/2 已各用一个实验 magic。
2. CLI：`fqc compress --max-ratio`、`--reference`、`--bam-sidecar`、`--paf` 的最终命名；
   当前只有开发工具 `fqc_ref_archive`。
3. 参考默认 external 还是 embedded；是否支持“只嵌入实际引用片段”。
4. BAM sidecar 是否要求 name-sorted；是否接受 coordinate-sorted + 外部排序。
5. 质量 codec：fqzcomp `v1.6.7` 已 vendored 且本地验证；zstd 回退的容器语义已冻结在
   `fqc-max/v1` / `fqc-max-ref/v1` 实验格式中，正式格式仍需复审。
6. 序列后端：zstd level 19 是否足够；是否引入 LZMA（liblzma/7zip）或 libbsc（Apache-2.0）。
7. ID codec：通用 front coding 已实现并通过真实 ID 探针；数字 token/delta、字段字典、
   跨块模型和 paired-end 联合 ID 编码仍需真实 WGS 归档验证。
8. contig 构造：桶大小、minimizer k、是否允许跨桶合并；内存/磁盘 spill 策略。
9. 是否提供 `--reorder` 实验开关；默认保序不变。
10. 长读是否进入同一容器，还是单独 `fqc-max-long/v1`。
11. 是否支持随机访问/块索引；第一版建议不支持。
12. 参考身份从 XXH64 升级到 SHA-256 的迁移路径；当前原型不满足最终要求。

这些决策必须在实现前收敛；未收敛项不得写成“已支持”。

## 15. 参考来源与可追溯矩阵

### 15.1 结论 → 来源

| 结论 | 证据等级 | 来源 | 本地复测 |
|---|---|---|---|
| Genozip 5.99×、ORA 5.64×、SPRING 3.79×、repaq 1.99×（vs gzip） | 独立 benchmark | Betschart et al., *Sci Rep* 2025, DOI 10.1038/s41598-025-00491-8 | 否 |
| Genozip benchmark 使用参考与双端 `--pair` | 独立 benchmark 补充材料 | 同上 Supplementary A | 否（已读取补充材料命令） |
| ORA 235 GB raw → 55 GB gz → 11 GB ORA | 厂商自报 | Illumina genomic data compression 页面 | 否 |
| ORA 序列 0.3–0.5 bits/nt、质量 range coder、ID token 化 | 厂商格式说明 | Illumina “Design considerations … ORA” | 否 |
| Genozip 31 GB gz → 6.0 GB，24.7× raw / 5.1× gz | 厂商自报 | Genozip FASTQ 页面 | 否 |
| Genozip 外部参考 vs 嵌入参考 +0.23 bytes/base（人类约 +700 MB） | 厂商文档 | Genozip reference 页面 | 否 |
| PgRC2 主要压 DNA 流、bpb 不是完整 FASTQ 比 | 论文自报 | PgRC2, *Bioinformatics* 2025 | 否 |
| Mstcom 相对 SPRING 平均约 22%、内存/时间高 | 论文自报 | Mstcom, *PLoS Comput Biol* 2021 | 否 |
| FQSqueezer 质量有 96/8/4/2/none 档 | 论文自报 | FQSqueezer, *Bioinformatics* 2020 | 否 |
| fqxv NovaSeq 24.0×/20.4×，GAIIx 11.97×/10.48× | 项目自报 | fqxv `docs/benchmarks.md` | 否 |
| FastqCA lossy 13.46–18.48×、lossless 最高约 11.05× | 论文自报 | FastqCA, *Front Genet* 2026 | 否 |
| CoLoRd 示例约 22×（论文称最高 25×），含质量有损 | 论文自报 | CoLoRd, *Nat Methods* 2022 | 否 |
| NanoSpring 0.35–0.65 bpb 仅序列 | 论文自报 | NanoSpring, *Sci Rep* 2023 | 否 |
| 当前 FQC 本地 HiSeq 4.0006× raw / 1.197× gz | 本地实验 | 本文 §4.1；SHA 已记录 | 是 |
| Phase 0 probe 可重复输出流级字节、gzip/zstd 基线与质量统计 | 本地实验 | `tools/ratio_probe`、`tests/tools/ratio_probe_test.cpp`、本文 §12.6 | 是 |
| 真实 HiSeq 上 zstd19 全文件 1,617,921 B、quality 流 681,586 B | 本地实验 | [本地 HiSeq 基线](max-ratio-baselines/2026-11-15-hiseq.md) | 是 |
| htscodecs fqzcomp 在 HiSeq 全质量上 558,189 B，比 zstd19 小 17.7% | 本地实验 | [质量 codec 基线](max-ratio-baselines/2026-11-15-quality-codecs.md) | 是 |
| fqzcomp 在 NovaSeq RNA-Seq 分箱质量头部上 2,159,788 B，比 zstd19 小 22.9% | 本地实验 | 同上 | 是 |
| fqzcomp 在 WGS 分箱质量头部上 2,168,649 B，比 zstd19 小 23.2% | 本地实验 | 同上 | 是 |
| fqzcomp 在 HiSeq 1k reads/块比整文件差 30.1%；建议 ≥4M bases/块 | 本地实验 | 同上 §5 | 是 |
| 质量 delta/transpose/6-bit 在真实 HiSeq 上更差 | 本地实验 | 本文 §8.1 | 是 |
| 合成数据 delta −18.3%，但真实数据 +1.5% | 本地实验 | 本文 §8.1、§12.3 | 是 |
| 质量流约占当前归档 51% | 本地实验 | 本文 §4.2 | 是 |
| `fqc-max/v1` 在 HiSeq 上 1,285,285 B，相对 v2 −21.36%、5.087× raw | 本地实验 | [实验容器基线](max-ratio-baselines/2026-11-15-max-container.md) | 是 |
| `fqc-max/v1` 在 NovaSeq/WGS 头部上 8.165×/7.790× raw，`max-verify` 与 `cmp` 通过 | 本地实验 | 同上 | 是 |
| 10×/20× 目标 | 设计目标 | 本文 §6.6、§7.5 | 未实现 |

### 15.2 外部来源

检索方法（2026-11-14）：以 “FASTQ compression benchmark 2025”“Illumina ORA FASTQ
compression”“Genozip FASTQ reference”“PgRC2 FASTQ”“Mstcom FASTQ”“FQSqueezer”
“fqxv benchmark”“FastqCA FASTQ”“CoLoRd”“NanoSpring” 等主题做 web 检索，优先读取
独立 benchmark、论文/PMC/期刊页和厂商技术页；最终以 URL/DOI 为可追溯来源，未保存原始
搜索查询串。外部页面内容可能变动，数字口径以正文标注为准，均未在本地复测。

- Betschart R.O., Sandberg F., Blankenberg S., Zoche M., Zeller T., Ziegler A.
  “A benchmark study of compression software for human short-read sequence data.”
  *Scientific Reports* 15, 15358 (2025). DOI: 10.1038/s41598-025-00491-8.
  <https://www.nature.com/articles/s41598-025-00491-8>
- Illumina, “Lossless Genomic Data Compression.”
  <https://www.illumina.com/science/technology/development/genomic-data-compression.html>
- Illumina, “Design considerations and methodology of .ORA format …”
  <https://www.illumina.com/science/genomics-research/articles/design-ora-lossless-genomic-compression.html>
- Genozip, “Compressing FASTQ.”
  <https://www.genozip.com/fastq>
- Genozip, “Reference files.”
  <https://www.genozip.com/reference>
- Lan D. et al., “Genozip: a universal extensible genomic data compressor.”
  *Bioinformatics* 37(16), 2225 (2021).
  <https://academic.oup.com/bioinformatics/article/37/16/2225/6135077>
- Kowalski T., Grabowski S., “PgRC2: engineering the compression of sequencing reads.”
  *Bioinformatics* 41(3), btaf101 (2025).
  <https://pmc.ncbi.nlm.nih.gov/articles/PMC11908645>
- Liu Y., Li J., “Hamming-shifting graph of genomic short reads …” (Mstcom).
  *PLoS Computational Biology* 17(7), e1009229 (2021).
  <https://journals.plos.org/ploscompbiol/article?id=10.1371/journal.pcbi.1009229>
- Deorowicz S. et al., “FQSqueezer: k-mer-based compression of sequencing data.”
  *Bioinformatics* 36(7), 2082 (2020).
  <https://pmc.ncbi.nlm.nih.gov/articles/PMC6969201>
- rnabioco, “fqxv benchmarks.”
  <https://github.com/rnabioco/fqxv/blob/main/docs/benchmarks.md>
- Long S. et al., “FastqCA: an effective FASTQ compressor through 2D spatial redundancy reduction.”
  *Frontiers in Genetics* 17, 1827471 (2026).
  <https://www.frontiersin.org/journals/genetics/articles/10.3389/fgene.2026.1827471/full>
- Kokot M., Gudyś A., Li H., Deorowicz S., “CoLoRd: compressing long reads.”
  *Nature Methods* 19, 441–444 (2022).
  <https://pmc.ncbi.nlm.nih.gov/articles/PMC9337911>
- “Reference-free lossless compression of nanopore sequencing reads using an approximate assembly approach.” (NanoSpring)
  *Scientific Reports* 13, 1999 (2023).
  <https://www.nature.com/articles/s41598-023-29267-8>
- htscodecs, custom CRAM codecs (rANS, fqzcomp-derived quality/name codecs).
  <https://github.com/samtools/htscodecs>

### 15.3 仓库内证据链

- 当前 fast 路径与取舍：[ALGORITHM.md](../ALGORITHM.md)
- 字节布局/内存模型：[ARCHITECTURE.md](../ARCHITECTURE.md)
- max-ratio 数据集 manifest：[docs/max-ratio-baselines/datasets.md](max-ratio-baselines/datasets.md)
- 质量 codec 实验：[docs/max-ratio-baselines/2026-11-15-quality-codecs.md](max-ratio-baselines/2026-11-15-quality-codecs.md)
- 旧调研：[docs/fastq-compression-survey.md](fastq-compression-survey.md)
- 路线图：[docs/roadmap.md](roadmap.md)
- 真实语料验收：[docs/real-corpus.md](real-corpus.md)
- v1 删除复盘：[docs/postmortems/2026-07-13-legacy-architecture-debt.md](postmortems/2026-07-13-legacy-architecture-debt.md)
- 决策：[docs/decisions/0003-max-ratio-mode.md](decisions/0003-max-ratio-mode.md)
- Phase 0 提案：[openspec/changes/2026-11-14-max-ratio-phase0-benchmark/proposal.md](../openspec/changes/2026-11-14-max-ratio-phase0-benchmark/proposal.md)
- Phase 0 工具：[tools/ratio_probe/README.md](../tools/ratio_probe/README.md)
- Phase 0 本地基线：[docs/max-ratio-baselines/2026-11-15-hiseq.md](max-ratio-baselines/2026-11-15-hiseq.md)
- Phase 1 实验容器基线：[docs/max-ratio-baselines/2026-11-15-max-container.md](max-ratio-baselines/2026-11-15-max-container.md)
- Phase 1 格式契约：[openspec/changes/2026-11-15-max-ratio-phase1-container/design.md](../openspec/changes/2026-11-15-max-ratio-phase1-container/design.md)
- Phase 2 参考原型基线：[docs/max-ratio-baselines/2026-11-15-ref-container.md](max-ratio-baselines/2026-11-15-ref-container.md)
- Phase 2 参考格式契约：[openspec/changes/2026-11-15-max-ratio-phase2-ref/design.md](../openspec/changes/2026-11-15-max-ratio-phase2-ref/design.md)
- Phase 2 实现笔记：[.agents/notes/implemented/architecture/2026-11-15-max-ratio-phase2-ref.md](../.agents/notes/implemented/architecture/2026-11-15-max-ratio-phase2-ref.md)
- 实现笔记：[.agents/notes/implemented/testing/2026-11-15-ratio-probe.md](../.agents/notes/implemented/testing/2026-11-15-ratio-probe.md)
- 质量探针笔记：[.agents/notes/implemented/testing/2026-11-15-quality-codec-probe.md](../.agents/notes/implemented/testing/2026-11-15-quality-codec-probe.md)
- 容器实现笔记：[.agents/notes/implemented/architecture/2026-11-15-max-ratio-phase1-container.md](../.agents/notes/implemented/architecture/2026-11-15-max-ratio-phase1-container.md)
- 参考原型实现笔记：[.agents/notes/implemented/architecture/2026-11-15-max-ratio-phase2-ref.md](../.agents/notes/implemented/architecture/2026-11-15-max-ratio-phase2-ref.md)
- 提案笔记：[.agents/notes/proposed/architecture/2026-11-14-max-ratio-mode.md](../.agents/notes/proposed/architecture/2026-11-14-max-ratio-mode.md)

### 15.4 更新规则

- 新增外部数字必须补“证据等级 + 口径 + 是否本地复测”；不能只贴一个倍率。
- 本地实验必须记录数据集 SHA、命令、工具版本；不能把 `/tmp` 探针写成正式 baseline。
- 实现落地后，本文的状态要从“设计完成；Phase 0/质量探针已实现”继续改为
  “部分实现/已实现”，并把【设计目标】替换为【本地实验】或【独立 benchmark】证据。
- 任何格式/magic 变更先走 `openspec/changes/`，不在本文直接冻结。

### 15.5 本次工作记录（2026-11-14 至 2026-11-15）

时间边界：按仓库文件时间戳，本轮可追溯的落地时间为：

- Phase 0/1 代码与实验：**2026-11-15 00:08–01:31（约 1 小时 23 分）**；
- Phase 0/1 文档收尾：**01:31–01:48（约 17 分钟）**；
- Phase 2 参考原型代码/实验：**约 01:48–02:04（约 16 分钟）**；
- Phase 2 文档、契约、最终门禁与交付复核：**02:04–02:34（约 30 分钟）**；
- Phase 2.1 ID codec 实现、真实 ID 探针、结构化 ID 实验、契约/文档与最终门禁：
  **02:34–03:08（约 34 分钟）**；
- Phase 2.2 clip/indel CIGAR 实现、合成基准、契约/文档与门禁：
  **2026-11-15 09:45–10:00（约 15 分钟）**；
- 真实 WGS R1 头部 + 完整 hg38 下载/索引/比对/归档/soft-mask 修复/文档/门禁：
  **约 10:20–11:10（约 50 分钟，含网络下载）**；
- CI clang-tidy/sanitizer 门禁恢复、clang-tidy 全量复核与真实归档回归：
  **约 11:10–11:35（约 25 分钟）**。

合计约 **4 小时 30 分**；前期 web 检索与方案推演没有可靠起始时间，不计入这个数字，也不把
它写成精确工时。

已完成：

- 检索并核对 ORA、Genozip、PgRC2、Mstcom、FQSqueezer、SPRING、fqxv、FastqCA、CoLoRd、
  NanoSpring 与 2025 年独立 benchmark 的公开数字、口径和许可证边界；
- 用本地真实 HiSeq 小样本测出当前 FQC 的流级体积构成，并做了质量值 delta/transpose/6-bit、
  ID front coding/comment 字典、帧大小与 zstd 分配微基准；
- 落盘本设计、ADR-0003、提案笔记、调研报告 §9、路线图、README/ALGORITHM/ARCHITECTURE
  的适用范围修正，以及 `CHANGELOG.md` 的未发布记录；
- 建立 Phase 0 OpenSpec 提案，明确“先测量、后格式”的门禁；
- 实现并测试 `tools/ratio_probe`（`fqc-ratio-probe/v1`），补充 CRLF、`+` 行、空 read、
  无最终换行与 gzip 输入的测试；
- 记录本地 HiSeq 的 raw/gzip/zstd/FQC 基线、输入 SHA 与 JSON 输出 SHA；
- 用 htscodecs `v1.6.7` 验证 fqzcomp：HiSeq 全质量比 zstd19 小 17.7%，NovaSeq
  RNA-Seq 分箱质量头部小 22.9%，WGS 分箱质量头部小 23.2%，三者都通过 Phase 1
  “≥10% 质量流缩减”门禁；
- 固定 NovaSeq `DRR174812` 头部子集与 WGS `SRR14724532` 头部子集/manifest，记录 ENA
  元数据、MD5、本地 SHA 与口径边界；
- 实现开发探针 `tools/quality_probe`（不安装、不进入主 CLI），输出
  `fqc-quality-probe/v1`，验证 fqzcomp round-trip 与分块曲线；
- 实现实验容器 `fqc-max/v1` 与 `max-compress`/`max-decompress`/`max-verify`，vendor
  htscodecs `v1.6.7` 的 fqzcomp 最小子集；真实 HiSeq/NovaSeq/WGS 头部归档分别为
  1,285,285 / 12,280,644 / 12,828,169 B，相对 v2 −21.36%～−24.87%，`max-verify` 与
  `cmp` 均通过；
- 实现 Phase 2 开发工具 `fqc_ref_archive` + `fqc-max-ref/v1`（PAF sidecar + external
  reference + fqzcomp quality）；合成 5 Mb 参考 / 3× 双端 150 bp 的 R1/R2 归档为
  827,688 / 827,546 B，约 21.05× raw；`verify` 与解压 `cmp` 通过，重复压缩 SHA 一致；
- 实现 Phase 2.1 自适应 ID front coding（`idCodec=2`），真实 WGS/NovaSeq 头部 ID 探针
  约 −99%，随机 wgsim ID 回退 raw 且旧归档逐字节不变；结构化 ID 合成归档 331,553 B
  （53.385× raw），并新增对应 OpenSpec、基线和实现笔记；
- 实现 Phase 2.2 CIGAR 扩展：支持 PAF M/=/X/I/D 与左右 clip，纯 M 保持旧布局；
  R1/R2 792,217/796,633 B（21.993×/21.871× raw），结构化 ID R1 296,082 B
  （59.781× raw），新增 `clipped`/`indel` 统计、OpenSpec、基线和实现笔记；
- 完成真实 WGS R1 头部 + 完整 hg38 小切片验收：修复 soft-masked 参考大小写，
  mapped 296,343/298,974，归档 4,547,935 B（21.974× raw），三路账和
  gzip/zstd/FQC/CRAM 对照已落盘；新增真实基线、复现脚本、一页纸摘要和 bug-fix 笔记；
- 新增 Phase 2 / Phase 2.1 / Phase 2.2 OpenSpec 契约、参考原型与 ID/CIGAR/真实 WGS
  基线、开发工具 README、实现笔记，并同步更新 README/ALGORITHM/ARCHITECTURE/roadmap/
  development-journey/CHANGELOG。

未完成 / 未声称：

- 没有 `--max-ratio` 顶层开关、内置 mapper、BAM sidecar、参考嵌入、S/H/N/P、长读
  spliced alignment、伪基因组、read 重排或质量上下文编码器；Phase 3 未实现，Phase 2
  只有 PAF 原型；
- 当前 `fqc-max/v1` / `fqc-max-ref/v1` 只对 canonical LF 输入做逐字节 round-trip；
  `+` 行内容、混合行尾、空 read 的原始字节保真未实现，不能当作最终 max-ratio；
- Phase 2.2 的 21.993×/21.871× 与结构化 ID 的 59.781× 都是合成数据、参考不计入、恒定
  Q23、PAF CIGAR 的机制验证；59.781× 还依赖人工结构化 ID，不是人类 30× WGS 结论；
- 真实 WGS 已完成 R1 头部 + 完整 hg38 小切片（21.974×），但完整 30× run、paired-end、
  参考计入/不计入两套完整 run 数字和外部工具同数据对比仍未完成；
- 没有在本地复测 ORA/Genozip/SPRING/PgRC2/Mstcom/fqxv；
- WGS `SRR14724532` 只有头部约 300k reads，完整 35× run 未下载；完整 run 的序列冗余/
  覆盖分布未验证；
- 所有 10×/20× 数字仍是设计目标；已实现容器的 5.09×–8.17× raw、合成参考原型 Phase 2.2
  的 21.993×/21.871× 与结构化 ID 的 59.781× 不是同一口径的第一梯队结论。

可追溯入口：

- 设计与证据矩阵：本文 §15.1–§15.2；
- 外部调研增补：[docs/fastq-compression-survey.md §9](fastq-compression-survey.md#9-2026-11-最高压缩比专项增补)；
- 决策：[ADR-0003](decisions/0003-max-ratio-mode.md)；
- Phase 0 提案：[openspec/changes/2026-11-14-max-ratio-phase0-benchmark/proposal.md](../openspec/changes/2026-11-14-max-ratio-phase0-benchmark/proposal.md)；
- Phase 1 容器契约：[openspec/changes/2026-11-15-max-ratio-phase1-container/design.md](../openspec/changes/2026-11-15-max-ratio-phase1-container/design.md)；
- Phase 1 容器基线：[docs/max-ratio-baselines/2026-11-15-max-container.md](max-ratio-baselines/2026-11-15-max-container.md)；
- Phase 2 参考契约：[openspec/changes/2026-11-15-max-ratio-phase2-ref/design.md](../openspec/changes/2026-11-15-max-ratio-phase2-ref/design.md)；
- Phase 2 参考基线：[docs/max-ratio-baselines/2026-11-15-ref-container.md](max-ratio-baselines/2026-11-15-ref-container.md)；
- Phase 2.1 ID codec 基线：[docs/max-ratio-baselines/2026-11-15-id-codec.md](max-ratio-baselines/2026-11-15-id-codec.md)；
- Phase 2.1 ID codec 契约：[openspec/changes/2026-11-15-max-ratio-phase2-id-codec/design.md](../openspec/changes/2026-11-15-max-ratio-phase2-id-codec/design.md)；
- Phase 2.2 CIGAR 基线：[docs/max-ratio-baselines/2026-11-15-ref-cigar.md](max-ratio-baselines/2026-11-15-ref-cigar.md)；
- Phase 2.2 CIGAR 契约：[openspec/changes/2026-11-15-max-ratio-phase2-cigar/design.md](../openspec/changes/2026-11-15-max-ratio-phase2-cigar/design.md)；
- 真实 WGS 基线：[docs/max-ratio-baselines/2026-11-15-real-wgs-srr14724532.md](max-ratio-baselines/2026-11-15-real-wgs-srr14724532.md)；
- 一页纸摘要：[docs/max-ratio-one-pager.md](max-ratio-one-pager.md)；
- 容器实现笔记：[.agents/notes/implemented/architecture/2026-11-15-max-ratio-phase1-container.md](../.agents/notes/implemented/architecture/2026-11-15-max-ratio-phase1-container.md)；
- 参考原型笔记：[.agents/notes/implemented/architecture/2026-11-15-max-ratio-phase2-ref.md](../.agents/notes/implemented/architecture/2026-11-15-max-ratio-phase2-ref.md)；
- 变更记录：[CHANGELOG.md](../CHANGELOG.md)。
