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
paired: R1/R2 锁步 -> 同一 block 交错 -> R2 同 contig 时存相对 R1 的坐标 delta
解压只需要同一份参考
```

## 三个关键数字

真实数据：`SRR14724532` R1/R2 各 **298,974 reads / 99,934,080 B** + 完整 UCSC hg38；
PAF 为 minimap2 primary alignment。**参考和 PAF 不计入主口径**。

| 方案 | 字节 | 相对原始 FASTQ |
|---|---:|---:|
| gzip -6（R1+R2） | 42,773,624 | 4.673× |
| zstd -19（R1+R2） | 33,412,892 | 5.982× |
| 当前 fast 路径（R1+R2） | 34,263,156 | 5.833× |
| CRAM（R1/R2 两个独立 alignment container） | 12,423,444 | 16.088× |
| 两个单端 `fqmr` 之和 | 9,678,812 | 20.650× |
| **paired `fqmr`** | **8,825,785** | **22.646×** |

1. **22.646× vs 20.650×**：paired 相对两个单端归档之和节省 **853,027 B（8.813%）**；R1/R2 单端分别为 21.974× / 19.477×。
2. **92.80% mate delta**：277,453/298,974 对 R2 与 R1 同 contig，使用相对坐标；alignment 流 4,939,473 → 4,094,927 B（−17.10%），quality 几乎中性（+0.046%）。
3. **98.7% mapped**：590,436/597,948；三路账为 ID 6,880 B、quality 4,711,904 B、alignment 4,094,927 B。真实 WGS 的主要矛盾仍是 quality 与 alignment，不是 ID。

## 三个关键取舍

| # | 取舍 | 代价/理由 |
|---|---|---|
| 1 | 先压缩率，后速度 | Phase 0–3 不设吞吐门禁；当前只记录运行事实，不做速度结论 |
| 2 | external reference + PAF，不内置 mapper | 先用最小机制验证“位置 + 编辑脚本”可无损重建；BAM/内置 mapper 后置 |
| 3 | paired 只做容器与坐标 delta | 不做跨 mate 共享 ID、联合质量或 insert-size 模型；质量流几乎没有额外收益 |

## 三个已知边界

| # | 边界 | 影响 |
|---|---|---|
| 1 | **不是完整 30× WGS** | R1/R2 各约 30 万 reads 的头部小切片；未做完整 run、ORA/Genozip 同数据复测或完整参考摊销 |
| 2 | **不是正式格式** | `fqc-max-ref/v1` 是实验工具；参考身份仍是 XXH64；无 SHA-256、参考嵌入、原子输出、内存上限、正式 CLI 开关；`S/H/N/P`、长读 spliced alignment、BAM/CRAM sidecar 未实现 |
| 3 | **口径不是“免费参考”** | 主数字是 external reference + external PAF 不计入；计入 PAF 为 1.874×，计入完整 3.27 GB 参考为 0.0609×；CRAM 是两个独立 alignment container 之和，不是 FASTQ 归档等价物 |

## 30 秒面试讲法

> fast 路径用列式分离、2-bit 序列、zstd 和有界并行流水线，解决流式、低内存、可校验；
> 真实 WGS R1 头部约 5.9×。最高压缩比另开独立路线：FASTQ + PAF + 外部参考，把 read
> 变成位置/方向/mismatch/CIGAR 编辑脚本，质量流用 fqzcomp；paired 再把 R2 坐标写成相对
> R1 的 delta。真实 R1/R2 头部 + 完整 hg38 得到 **22.646×**，两个单端之和是 20.650×。
> 但参考/PAF 不计入，不是完整 30× WGS。

## 下一步

1. 完整 30× paired run，报告参考计入/不计入两套数字；
2. ORA/Genozip 同数据同口径对比；CRAM 已作大小参考，但不是功能等价；
3. 评估跨 mate 共享 ID、质量上下文和 insert-size 模型；
4. 最后才是并发、内存上限、SIMD 与正式格式冻结。

## 证据

- [真实 WGS paired 基线与复现](max-ratio-baselines/2026-11-15-real-wgs-paired.md)
- [真实 WGS R1 基线](max-ratio-baselines/2026-11-15-real-wgs-srr14724532.md)
- [max-ratio 设计与证据矩阵](max-ratio-design.md) · [业界调研](fastq-compression-survey.md)
- [Phase 2.2 CIGAR 基线](max-ratio-baselines/2026-11-15-ref-cigar.md) · [ADR-0003](decisions/0003-max-ratio-mode.md)
- [复现脚本](../scripts/fetch_real_wgs_ref_bench.sh) · [CHANGELOG](../CHANGELOG.md)
