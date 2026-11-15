# FQC 一页纸：fast 流式压缩与 max-ratio 参考压缩

> 2026-11-15 · 面试/技术报告摘要 · 结论与口径优先，证据见文末。

## 项目目标

- **fast 路径（默认）**：C++23 流式、内存有界、可校验、管道友好的 FASTQ 压缩；压缩比让位于工程可用性。
- **max-ratio 路径（实验）**：先不考虑速度，用 external reference + PAF + 专用质量编码追绝对压缩比；独立格式/入口，不破坏 `fqc-sequential/v2`。

## 架构

```text
fast:  FASTQ -> parser×K -> [有界 MPMC] -> encoder×N（2-bit + zstd + XXH64）
              -> [MPMC] -> ChunkOrderer 保序 -> .fqc
解压:  reader -> decoder×N -> reorder -> 滚动校验和 -> FASTQ

max-ref（fqc-max-ref/v1，开发工具 fqc_ref_archive）:
FASTQ + PAF + external reference -> ID / quality / alignment 三路
  -> 位置+方向+mismatch+clip/indel 编辑脚本
  -> front-coded ID + fqzcomp quality + zstd alignment -> .fqmr
解压只需要同一份参考
```

## 三个关键数字

真实数据：`SRR14724532` R1 头部 **298,974 reads / 99,934,080 B** + 完整 UCSC hg38；
PAF 为 minimap2 primary alignment。**参考和 PAF 不计入主口径**。

| 方案 | 字节 | 相对原始 FASTQ |
|---|---:|---:|
| gzip -6 | 21,097,162 | 4.737× |
| zstd -19 | 16,463,409 | 6.070× |
| 当前 fast 路径 | 16,811,413 | 5.944× |
| CRAM（external hg38） | 5,929,273 | 16.854× |
| **`fqc-max-ref/v1`** | **4,547,935** | **21.974×** |

> CRAM 是 alignment container，不是 FASTQ 字节归档；上表只作大小参考，不是功能等价或全面胜出。

1. **21.974×**：参考/PAF 不计入；计入 PAF 降到 1.865×，计入完整 3.27 GB 参考降到 0.0305×。
2. **99.1% mapped**：296,343/298,974，literal 仅 2,631 条。
3. **三路账**：ID 3,701 B（0.08%）、quality 2,181,008 B（47.96%）、
   alignment 2,352,120 B（51.72%）；真实 WGS 的主要矛盾是 quality 与 alignment，不是 ID。

## 三个关键取舍

| # | 取舍 | 代价/理由 |
|---|---|---|
| 1 | 先压缩率，后速度 | Phase 0–3 不设吞吐门禁；当前只记录运行事实，不做速度结论 |
| 2 | external reference + PAF，不内置 mapper | 先用最小机制验证“位置 + 编辑脚本”可无损重建；BAM/内置 mapper 后置 |
| 3 | 复用成熟编码器 | 质量用 vendored htscodecs fqzcomp，alignment/ID 用 zstd19；不自研 range coder |

## 三个已知边界

| # | 边界 | 影响 |
|---|---|---|
| 1 | **不是完整 30× WGS** | 只有一个 R1 头部约 30 万 reads；未做 paired-end、完整 run 覆盖分布、ORA/Genozip 同数据复测 |
| 2 | **不是正式格式** | `fqc-max-ref/v1` 是实验工具；参考身份仍是 XXH64；无 SHA-256、参考嵌入、原子输出、内存上限、正式 CLI 开关；`S/H/N/P`、长读 spliced alignment、BAM/CRAM sidecar 未实现 |
| 3 | **口径不是“免费参考”** | 主数字是 external reference, not counted；小切片计入参考仅 0.0305×；完整 run 必须同时报告计入/不计入两套数字，不能宣称业界第一 |

## 30 秒面试讲法

> fast 路径用列式分离、2-bit 序列、zstd 和有界并行流水线，解决流式、低内存、可校验；
> 真实 WGS R1 头部约 5.9×。最高压缩比另开独立路线：FASTQ + PAF + 外部参考，把 read
> 变成位置/方向/mismatch/CIGAR 编辑脚本，质量流用 fqzcomp；真实 R1 头部 + 完整 hg38
> 得到 21.974×、99.1% mapped。但参考/PAF 不计入，不是完整 30× WGS。

## 下一步

1. 完整 30× WGS 的固定分片/paired-end 验收，报告参考计入/不计入两套数字；
2. ORA/Genozip 同数据对比（CRAM 已作大小参考，但不是功能等价）；根据三路账决定 quality / alignment / ID 的优化优先级；
3. 最后才是并发、内存上限、SIMD 与正式格式冻结。

## 证据

- [真实 WGS 基线与复现](max-ratio-baselines/2026-11-15-real-wgs-srr14724532.md)
- [max-ratio 设计与证据矩阵](max-ratio-design.md) · [业界调研](fastq-compression-survey.md)
- [Phase 2.2 CIGAR 基线](max-ratio-baselines/2026-11-15-ref-cigar.md) · [ADR-0003](decisions/0003-max-ratio-mode.md)
- [复现脚本](../scripts/fetch_real_wgs_ref_bench.sh) · [CHANGELOG](../CHANGELOG.md)
