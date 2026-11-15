# Change Proposal: max-ratio-phase0-benchmark

## Metadata

- Status: `Implemented (measurement scope); archive pending review`
- Repository: `open-genomics/fq-compressor`
- Date: 2026-11-14
- Capability: `max-ratio-research`（Phase 0 测量底座；尚不改变 `archive-format`）
- Decision IDs: `ADR-0003`
- Design: [docs/max-ratio-design.md](../../../docs/max-ratio-design.md)
- ADR: [docs/decisions/0003-max-ratio-mode.md](../../../docs/decisions/0003-max-ratio-mode.md)

## Why

2026-11 的最高压缩比调研已经确认：当前 `fqc-sequential/v2` fast 路径约 4.00× 原始 FASTQ，
差距来自算法结构而不是 zstd 档位；要进入 10×–20× 区间，需要参考映射或全局伪基因组，并把
质量值从“原始 ASCII + zstd”升级为上下文模型。

但在写任何新格式或编码器之前，仓库缺少一个可重复的、按流计量的压缩比底座：

- 现有本地实验是临时 Python 探针，未提交脚本，不能作为正式 baseline；
- 当前 `FastqParser` 丢弃 `+` 行内容、规范化 CRLF、拒绝空 read，不能直接承担 max-ratio
  的逐字节无损测量；
- 外部论文/厂商数字口径混杂（raw、fastq.gz、bits/base、有损质量），不能直接拿来验收。

Phase 0 只解决“测得准、可追溯”，不实现 max-ratio 容器。

## Changes

- 固定首批真实数据集与 SHA-256、下载日期、read 数、覆盖度等元数据；HiSeq、NovaSeq
  RNA-Seq 头部、WGS 头部均已固定，WGS 完整 35× run 仅 manifest。
- 增加一个**仅供开发/验收**的 ratio-only 探针，按流输出：
  - record count、bases、N count、质量符号数、平均 Q；
  - ID/comment、sequence、quality、`+` 行、换行的原始字节；
  - sequence packed 大小、quality/ID 的固定参数 zstd 基线；
  - raw/gzip/归档三类压缩比与 bits/base。
- 建立当前 FQC、`gzip -6`、`zstd -19`（以及可获得的 fqzcomp/SPRING/PgRC2）基线表。
- 把探针输出与数据集 manifest 纳入可追溯文档；探针不安装、不进入产品 CLI。

## Scope

- `docs/`：测量协议、数据集 manifest、结果记录与边界说明。
- `openspec/changes/`：本 Phase 0 提案与验收记录。
- 后续实现（需单独授权）：开发用 ratio probe、测试与构建目标。

## Out of scope

- 不实现 `--max-ratio`、`--reference`、`--bam-sidecar` 或任何产品 CLI。
- 不新增 magic、格式族、帧布局或 codec ID；`fqc-sequential/v2` 字节不变。
- 不实现参考 mapper、伪基因组、质量上下文编码器或速度优化。
- 不把临时探针数字写成正式 benchmark 或对外承诺。

## Compatibility and rollback

- 默认 fast 路径、归档格式、CLI 行为不变；本提案不产生新归档字节。
- ratio probe 是开发工具，失败或删除不影响产品路径。
- 若探针无法稳定复现，回滚范围为探针与结果文档，不触碰 `fqc-sequential/v2`。

## Approval

- 设计方向：`ADR-0003` 已采纳（设计方向，待实现）。
- 本轮授权：用户在 max-ratio 专项中要求持续推进设计/实现，允许任务范围内添加开发工具、
  测试与文档；不包含生产格式、提交、推送或部署。
- Apply approval: `measurement scope applied; production format pending`。
