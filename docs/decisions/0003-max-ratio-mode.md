# ADR-0003: 最高压缩比作为独立 opt-in 模式

- 状态：**已采纳（设计方向；Phase 0 探针/数据集 manifest、Phase 1 质量 codec 探针与实验容器 `fqc-max/v1`、Phase 2 PAF 参考原型 `fqc-max-ref/v1`、Phase 2.1 自适应 ID front coding、Phase 2.2 clip/indel CIGAR 已实现；Phase 2 仅合成机制验证，真实 30× WGS 与 Phase 3 未实现；最终格式未冻结）**
- 日期：2026-11-14
- 关联：[docs/max-ratio-design.md](../max-ratio-design.md)、
  [docs/fastq-compression-survey.md](../fastq-compression-survey.md)、
  [Phase 0 OpenSpec 提案](../../openspec/changes/2026-11-14-max-ratio-phase0-benchmark/proposal.md)、
  [.agents/notes/proposed/architecture/2026-11-14-max-ratio-mode.md](../../.agents/notes/proposed/architecture/2026-11-14-max-ratio-mode.md)

## 背景

`fqc-sequential/v2` 的 fast 路径以流式、帧独立、内存有界、快速可校验为目标。它在真实
HiSeq 小样本上约 4.00× 原始 FASTQ，但质量流仍是原始 ASCII + zstd，序列只做 2-bit 打包，
没有跨 read 参考或重排。2025 年独立 benchmark 与 ORA/Genozip、SPRING、PgRC2、Mstcom、
FQSqueezer、fqxv 等方案表明：要进入 10×–20× 区间，必须改变算法结构，而不是继续调 zstd
档位、帧大小或朴素质量变换。

同时，v1 曾因重排序、编码器状态与内存核算耦合错位删除 27,835 行；把全局分析直接塞进
现有帧流水线会重演同类问题。因此需要把“最高压缩比”作为独立模式，而不是改 fast 路径的
默认行为。

## 决策

1. **`fqc-sequential/v2` 与当前 CLI 保持原样。** fast 路径继续服务流式、低内存、快速
   round-trip 场景；不因压缩比专项改变默认格式、帧语义或速度取舍。
2. **新增独立的 opt-in `max-ratio` 方向，使用独立格式族/版本与独立验收。** Phase 1
   实验容器 `fqc-max/v1` 与 `max-compress`/`max-decompress`/`max-verify`、Phase 2
   开发工具 `fqc_ref_archive` 与 `fqc-max-ref/v1`、Phase 2.1 自适应 ID front coding、
   Phase 2.2 M/=/X/I/D + clip 编码已实现；
   最终 magic/CLI/字节布局仍待 `openspec/changes/` 与后续 ADR 冻结。
3. **两条 profile：**
   - `max-ref`：参考/PAF/BAM sidecar + 位置/方向/差异 + 质量上下文，追求绝对最高压缩比；
   - `max-ref-free`：minimizer/伪基因组 + 原序引用 + 质量上下文，追求无参最高压缩比。
4. **默认保序，不把 permutation 作为默认代价。** 全局 contig 表可以在不重排 read 记录的情况下
   提供跨 read 参考；是否提供 `--reorder` 仅作为实验开关，需用真实数据证明收益。
5. **质量值优先复用成熟实现**：htscodecs fqzcomp 为默认候选，每块试策略 0/1 并取较小者，
   zstd 仅作失败/无收益回退；本地 HiSeq 全质量、NovaSeq RNA-Seq 分箱质量与 WGS 分箱
   质量样本均验证 fqzcomp 优于 zstd19（−17.7% / −22.9% / −23.2%）。不自研 range coder，不把有损质量档混入默认路径。
6. **压缩比优先，速度后置；绝对压缩比优先。** Phase 0–3 不设吞吐门禁；Phase 2 先做
   有参 `max-ref`，Phase 3 再做无参 `max-ref-free`；Phase 4 才讨论并行、SIMD、内存复用。
7. **实现前必须完成：** Phase 0 benchmark 底座、`openspec/changes/` 格式契约、内存/spill
   设计、参考摘要/解压依赖设计、危险输入的 fail-closed 验收。Phase 1 容器为追溯补契约，
   仅冻结实验格式；正式对外格式仍需复审。

## 后果

- 优点：fast 路径不被高风险全局状态污染；最高压缩比有独立演进空间；两条 profile 可以分别
  对齐 ORA/Genozip 与 SPRING/PgRC2/Mstcom 的证据面；格式/CLI 未定前不会误导用户。
- 代价：仓库会出现两套压缩引擎与两套格式维护成本；`max-ref` 需要参考分发/版本绑定；
  `max-ref-free` 需要两趟、全局 contig 表、spill 和更高的实现复杂度；短期内没有可用的
  `--max-ratio` 命令。
- 需要重访的信号：Phase 1 质量流不能稳定缩减 ≥10%；Phase 2 参考依赖不可接受；
  Phase 3 在 16 GiB 预算下不能稳定超过 8× 原始；参考/格式复杂度超过个人项目可维护边界；
  外部工具已经提供足够好且可直接复用的归档格式。

## 备选方案

- **继续调 zstd level / 帧大小 / 质量 delta**：改动小，但真实数据上收益只有几个百分点，
  且本地实验显示 delta/transpose/6-bit 在 HiSeq 上反而更差；不能桥接 4× → 10×+。
- **只做 `max-ref`**：绝对压缩比最高、实现相对直接，但无参考数据完全失效，不能覆盖通用
  FASTQ 场景。
- **只做 `max-ref-free`**：通用性好，但达到 20×+ 更难，且两趟/全局状态与 v1 风险最接近。
- **直接复刻 SPRING/PgRC2/Mstcom**：能快速接近论文数字，但会继承它们的顺序/字段级无损边界、
  内存模型和维护成本；PgRC2/Mstcom 是 GPL-3.0、SPRING 含 research-use 限制，不能直接并入
  MIT 项目；且无法利用 FQC 已有的格式/校验/工程约束。
- **复用 htscodecs fqzcomp/rANS 作为全部方案**：质量值收益明确，但不能解决序列跨 read 冗余；
  只做质量编码不足以进入第一梯队。

## 验证

- 设计、证据与本地实验：[docs/max-ratio-design.md](../max-ratio-design.md)
- 旧调研的适用范围修正：[docs/fastq-compression-survey.md](../fastq-compression-survey.md)
- 路线图独立章节：[docs/roadmap.md](../roadmap.md)
- Phase 0 测量底座提案：[openspec/changes/2026-11-14-max-ratio-phase0-benchmark/proposal.md](../../openspec/changes/2026-11-14-max-ratio-phase0-benchmark/proposal.md)
- Phase 0 探针：[tools/ratio_probe/README.md](../../tools/ratio_probe/README.md)
- 数据集 manifest：[docs/max-ratio-baselines/datasets.md](../max-ratio-baselines/datasets.md)
- 质量 codec 实验：[docs/max-ratio-baselines/2026-11-15-quality-codecs.md](../max-ratio-baselines/2026-11-15-quality-codecs.md)
- 实验容器实测：[docs/max-ratio-baselines/2026-11-15-max-container.md](../max-ratio-baselines/2026-11-15-max-container.md)
- Phase 1 容器契约：[openspec/changes/2026-11-15-max-ratio-phase1-container/design.md](../../openspec/changes/2026-11-15-max-ratio-phase1-container/design.md)
- Phase 2 参考原型实测：[docs/max-ratio-baselines/2026-11-15-ref-container.md](../max-ratio-baselines/2026-11-15-ref-container.md)
- Phase 2 参考格式契约：[openspec/changes/2026-11-15-max-ratio-phase2-ref/design.md](../../openspec/changes/2026-11-15-max-ratio-phase2-ref/design.md)
- Phase 2.1 ID codec 实测：[docs/max-ratio-baselines/2026-11-15-id-codec.md](../max-ratio-baselines/2026-11-15-id-codec.md)
- Phase 2.1 ID codec 契约：[openspec/changes/2026-11-15-max-ratio-phase2-id-codec/design.md](../../openspec/changes/2026-11-15-max-ratio-phase2-id-codec/design.md)
- Phase 2.2 CIGAR 实测：[docs/max-ratio-baselines/2026-11-15-ref-cigar.md](../max-ratio-baselines/2026-11-15-ref-cigar.md)
- Phase 2.2 CIGAR 契约：[openspec/changes/2026-11-15-max-ratio-phase2-cigar/design.md](../../openspec/changes/2026-11-15-max-ratio-phase2-cigar/design.md)
- Phase 2.1 ID codec 笔记：[.agents/notes/implemented/architecture/2026-11-15-max-ratio-phase2-id-codec.md](../../.agents/notes/implemented/architecture/2026-11-15-max-ratio-phase2-id-codec.md)
- Phase 2 实现笔记：[.agents/notes/implemented/architecture/2026-11-15-max-ratio-phase2-ref.md](../../.agents/notes/implemented/architecture/2026-11-15-max-ratio-phase2-ref.md)
- 当前仓库状态：`fqc_ratio_probe`、`fqc_quality_probe`、`fqc-max/v1` 实验容器、
  `fqc_ref_archive` 参考原型、自适应 ID front coding 与 Phase 2.2 CIGAR 已实现；仍无
  `--max-ratio` 顶层开关、内置 mapper/BAM/参考嵌入、Phase 3 伪基因组或最终格式。
