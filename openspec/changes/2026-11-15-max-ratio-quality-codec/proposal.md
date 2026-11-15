# Change Proposal: max-ratio-quality-codec

## Metadata

- Status: `Implemented (research tool; production integration pending)`
- Repository: `open-genomics/fq-compressor`
- Date: 2026-11-15
- Capability: `max-ratio-research`（质量 codec 证据；尚不改变 `archive-format`）
- Decision IDs: `ADR-0003`
- Design: [docs/max-ratio-design.md](../../../docs/max-ratio-design.md)
- Baseline: [docs/max-ratio-baselines/2026-11-15-quality-codecs.md](../../../docs/max-ratio-baselines/2026-11-15-quality-codecs.md)
- Dataset manifest: [docs/max-ratio-baselines/datasets.md](../../../docs/max-ratio-baselines/datasets.md)
- Phase 0: [max-ratio-phase0-benchmark](../2026-11-14-max-ratio-phase0-benchmark/proposal.md)

## Why

Phase 0 量出质量流是当前 FQC 归档最大的一块，但“fqzcomp 是否值得进入 max-ratio 容器”
只有论文/厂商证据，没有本地、可复现、逐字节 round-trip 的实现证据。若先写容器，可能把
错误的块大小、策略或 codec 假设冻结进格式。

本 change 只验证质量 codec，不实现 max-ratio 归档。它回答三个问题：

1. fqzcomp 在 HiSeq 全质量、NovaSeq RNA-Seq 分箱与 WGS 分箱上是否稳定优于 zstd19；
2. 块大小/策略如何影响质量流体积；
3. 质量流替换对当前 FQC 归档的估算收益是否足以通过 Phase 1 门禁。

## Changes

- 新增开发专用 `tools/quality_probe` / `fqc_quality_probe`（不安装、不进入主 CLI）：
  - 解析 FASTQ quality 流，支持 LF/CRLF、空 read、literal `+` 行；
  - 调用 htscodecs `v1.6.7` fqzcomp，逐块 round-trip 校验；
  - 支持策略 0–3 与自适应 0/1；
  - zstd19 对照；输出 `fqc-quality-probe/v1` JSON；
  - 通过外部 htscodecs 源码树构建，不进入产品 CMake 目标。
- 固定 htscodecs `v1.6.7` 的 tag/commit/tarball SHA 与许可证边界。
- 固定 HiSeq、NovaSeq `DRR174812` 头部子集与 WGS `SRR14724532` manifest。
- 记录质量流结果、分块曲线、归档影响估算与未验证边界。
- 更新设计、路线图、ADR、CHANGELOG 与 Agent Note。

## Scope

- `tools/quality_probe/`：开发工具与构建脚本/README。
- `docs/max-ratio-baselines/`：数据集 manifest、质量 codec 基线。
- `docs/max-ratio-design.md`、`docs/roadmap.md`、`docs/decisions/0003-max-ratio-mode.md`。
- `CHANGELOG.md`、`.agents/notes/`。

## Out of scope

- 不实现 `--max-ratio`、新 magic、新格式族或块布局。
- 不修改 `src/`、`include/`、生产 `FastqParser`、`fqc-sequential/v2` 或默认 CLI。
- 不把 htscodecs 加入 Conan/产品构建；不 vendor 第三方源码。
- 不实现 ID tokeniser、paired-end read2/reverse、完整 WGS 下载或速度优化。
- 不把质量流替换估算写成已实现归档比。

## Compatibility and rollback

- 默认 fast 路径、归档格式、CLI 行为不变；本提案不产生新归档字节。
- `tools/quality_probe` 是开发工具，失败或删除不影响产品路径。
- 若 htscodecs 版本/许可证/构建成本不可接受，回滚范围为探针与结果文档；已固定的数据集
  manifest 和质量流结论仍可被其它 codec 复用。

## Approval

- 设计方向：`ADR-0003` 已采纳。
- 本轮授权：用户在 max-ratio 专项中要求持续推进设计/实现，允许任务范围内添加开发工具与
  文档；不包含生产格式、提交、推送或部署。
- Production integration: `pending`，需单独的 Phase 1 容器/格式 change。
