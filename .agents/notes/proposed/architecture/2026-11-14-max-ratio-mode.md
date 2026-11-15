# Agent Note: 压缩比专项：fast 与 max-ratio 分离

Status: proposed

## Problem

当前 `fqc-sequential/v2` 是流式、帧独立、内存有界的 fast 路径：质量值原始 ASCII + zstd，
序列 2-bit 打包，无跨 read 重排/参考。真实 HiSeq 小样本约 4.00× 原始 FASTQ；2025 年独立
benchmark 与 ORA/Genozip/SPRING/PgRC2/Mstcom 等方案表明，10×–20× 区间需要参考映射、
全局伪基因组/重排与质量上下文编码，不是 zstd 档位或帧大小能补上的。

同时，v1 的 ABC/SCM/ReorderMap 因重排序、编码器状态与内存核算耦合错位删除 27,835 行。
若把全局分析直接塞进现有帧流水线，会重演“内存上限无法强制执行”的问题。

## Proposal

新增独立的 opt-in `max-ratio` 方向，不改 fast 路径：

- 独立格式族/版本、独立 CLI 入口、独立验收；当前不实现，先补 `openspec/changes/` 与 ADR。
- `max-ref`：参考/BAM sidecar → 位置/方向/差异 + 质量上下文。
- `max-ref-free`：minimizer/伪基因组 → 原序引用 + 差异 + 质量上下文。
- 默认保序，不存 permutation；全局 contig 表 + 原始顺序 read 记录。
- 质量值优先复用 htscodecs fqzcomp，每块试策略 0/1 并取较小者，zstd 仅作回退；
  不自研 range coder，不混入有损质量档。
- Phase 0 benchmark 与内存/spill 设计先于编码；Phase 1–3 不设速度门禁。
- Phase 0 `fqc_ratio_probe` 与数据集 manifest 已实现（[实现笔记](../../implemented/testing/2026-11-15-ratio-probe.md)、
  [数据集清单](../../../../docs/max-ratio-baselines/datasets.md)）；
- Phase 1 质量 codec 探针已通过门禁：HiSeq 全质量 fqzcomp 比 zstd19 小 17.7%，
  NovaSeq RNA-Seq 分箱头部小 22.9%，WGS 分箱头部小 23.2%
  （[实验](../../../../docs/max-ratio-baselines/2026-11-15-quality-codecs.md)）；
  Phase 1 实验容器 `fqc-max/v1` 已实现，真实 HiSeq/NovaSeq 头部/WGS 头部为
  1,285,285 / 12,280,644 / 12,828,169 B，`max-verify`/`cmp` 通过
  （[容器基线](../../../../docs/max-ratio-baselines/2026-11-15-max-container.md)）。
- Phase 2 PAF 参考原型 `fqc-max-ref/v1` / `fqc_ref_archive` 已实现；合成 5 Mb/3×
  双端数据 R1/R2 约 21.05× raw，`verify`/`cmp` 通过；参考不计入、恒定 Q23、只接受纯
  `<len>M`，真实 30× WGS 未验收
  （[参考基线](../../../../docs/max-ratio-baselines/2026-11-15-ref-container.md)）。
- 实现顺序按绝对压缩比优先：Phase 2 先做有参 `max-ref`，Phase 3 再做无参 `max-ref-free`。

详细设计、证据矩阵与本地实验见
[docs/max-ratio-design.md](../../../../docs/max-ratio-design.md)；
决策见 [ADR-0003](../../../../docs/decisions/0003-max-ratio-mode.md)；
Phase 0 测量底座提案见
[openspec/changes/2026-11-14-max-ratio-phase0-benchmark/proposal.md](../../../../openspec/changes/2026-11-14-max-ratio-phase0-benchmark/proposal.md)。

## Alternatives considered

- **继续调 zstd level / 帧大小 / 质量 delta**：改动最小，但真实 HiSeq 上 delta +1.5%、
  transpose +13.1%、6-bit +14.9%；合成数据的 −18% 不能外推，无法桥接 4× → 10×+。
- **只做 `max-ref`**：绝对压缩比最高、实现路径相对直接（BAM sidecar 优先），但无参考数据
  完全失效，不能覆盖通用 FASTQ；且参考分发/版本绑定是长期成本。
- **只做 `max-ref-free`**：通用性最好，但达到 20×+ 更难，两趟/全局 contig/内存模型与 v1
  风险最接近；若 Phase 1 质量收益不足，继续投入不划算。
- **直接复刻 SPRING/PgRC2/Mstcom**：能快速接近论文数字，但会继承其保序/字段级无损边界、
  内存与维护成本；SPRING 的逐字节无损性还有争议。复刻不等于理解，也不一定适配 FQC 的
  校验/格式约束。
- **只复用 htscodecs fqzcomp**：质量流收益明确、许可相对友好，但不解决序列跨 read
  冗余；单靠质量编码不足以进入第一梯队。
- **继续维护单一 fast 引擎，不新增模式**：维护成本最低，但等于放弃 10×+ 目标；用户已明确
  先追求最高压缩比，故不作为当前选择。

## Acceptance criteria

- Phase 0：真实数据集 SHA/命令/版本固定；ratio-only 探针能按流报告 raw/gz/bpb；重复运行
  字节数稳定。
- Phase 1：真实 HiSeq/NovaSeq 上归档相对当前 FQC 至少 −15%；逐字节 round-trip 覆盖
  CRLF、`+` 行、IUPAC、空 read、paired-end；畸形输入 fail closed；速度不设门禁。
  质量流门禁已通过（HiSeq −17.7%、NovaSeq −22.9%、WGS 头部 −23.2% vs zstd19），
  归档 −15% 仍是估算。
- Phase 2：30× 人类短读 WGS ≥20× 原始、≥5× gzip；参考摘要/解压依赖严格校验；外部参考
  与嵌入参考两套体积都报告。
- Phase 3：全质量短读 ≥10× 原始；分箱 NovaSeq ≥18–20× 原始；16 GiB 预算内可完成或显式
  spill；深覆盖数据上超过 SPRING 3.79× vs gzip 的量级。
- 实现前：格式契约进 `openspec/changes/`，ADR-0003 复审；不得先写代码后补格式。

## Risks

- **内存/全局状态**：伪基因组、参考索引、contig 表可能超过 16 GiB；必须先做预算与 spill，
  否则重演 v1。缓解：显式 `MemoryPlan`、块级桶、磁盘 spill、kill criteria。
- **参考依赖**：`max-ref` 解压需要同一参考；摘要不符/参考缺失必须 fail closed，不能静默
  降级。缓解：SHA-256 + external/embedded 两种模式 + 明确体积口径。
- **BAM sidecar 顺序/字段**：BAM 不保留原始 `+` 行和原顺序；只能作为 sidecar，不能作为
  唯一输入。缓解：要求 name-sorted 或建索引，FASTQ 仍是字节真源。
- **质量模型反转**：本地 NovaSeq 分箱质量头部上 fqzcomp 仍赢 zstd19（−22.9%），但完整
  run、WGS 与其它仪器仍可能反转。缓解：每块自适应 codec，保留 zstd 回退。
- **有损数字污染**：FQSqueezer 低字母表、CoLoRd、FastqCA lossy、Genozip `--optimise`
  不能混入无损结论。缓解：证据等级/口径标签，默认路径拒绝有损。
- **许可/依赖**：htscodecs、LZMA、libbsc 的许可与构建成本需在引入前核对；PgRC/Mstcom/CoLoRd
  是 GPL-3.0，FQSqueezer/SPRING 条款也不适合直接复制。第一版优先 htscodecs + zstd，
  外部实现只作算法参考。
- **范围膨胀**：参考 mapper/assembler/长读可能把项目拖入完整比对器/组装器。缓解：首版
  短读、BAM sidecar 优先、长读单独轨道、停止条件写死。
