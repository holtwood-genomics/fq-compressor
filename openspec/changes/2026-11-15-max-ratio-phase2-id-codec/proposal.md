# Proposal: max-ratio Phase 2.1 自适应 ID front coding

- 日期：2026-11-15
- 状态：实验扩展已实现，待用户复核；不是最终对外格式
- 基础：[Phase 2 参考原型](../2026-11-15-max-ratio-phase2-ref/design.md)
- 基线：[2026-11-15-id-codec.md](../../../docs/max-ratio-baselines/2026-11-15-id-codec.md)

## 为什么

Phase 2 的 `fqc-max-ref/v1` 已经证明参考位置/差异编码能把序列成本压低，但在合成归档中
ID 流仍占 `496,574 / 827,688 B`，是最大单项。真实 Illumina WGS 的 read ID 有稳定的
run/flowcell/lane/tile/x/y 结构，简单 zstd 不能充分消掉逐条记录之间几乎相同的前缀。

本地 header-only 探针显示：

- WGS `SRR14724532` 头部 ID：zstd19 `114,099 B` → front-coded zstd19 `1,085 B`；
- NovaSeq `DRR174812` 头部 ID：`111,762 B` → `1,105 B`；
- 随机 wgsim ID：`495,131 B` → `545,494 B`，front coding 反而更差。

因此需要“按块自适应选择”，而不是全局强制 front coding。

## 做什么

在 `fqc-max-ref/v1` 的 block header 既有 `idCodec` 字段上增加：

- `idCodec=1`：raw ID 逻辑流 + zstd19（原行为）；
- `idCodec=2`：front-coded ID 逻辑流 + zstd19（新行为）；
- 编码端同时生成两种逻辑流，各自 zstd19，按块取压缩后更小者；
- 尾部 4 字节保留位保存 front-coded 逻辑流未压缩长度（raw 时写 0）；
- block checksum 仍覆盖解码后的 raw ID 逻辑流；
- 解码端校验 front-coded 前缀/后缀边界、重建 raw 流、校验长度和 checksum。

## 不做什么

- 不改变 global magic/version 或默认 `fqc` CLI；
- 不做数字 token/delta、字段字典、跨块 ID 模型；
- 不做 paired-end 联合 ID 编码；
- 不把结构化 ID 合成实验写成真实 30× WGS 结论；
- 不改变参考摘要仍是 XXH64、参考不计入归档等 Phase 2 边界。

## 验收

- 原始 wgsim R1 重新压缩仍选择 raw，归档逐字节等于改动前，SHA
  `23fe0fd1ef9cfef84c60195b6a86b79e31d2159f59f5c5a751d95b61ee42b2d5`；
- 结构化 ID 合成 R1：17,700,000 B → 331,553 B（53.385× raw），`front_id_blocks=2`，
  `verify` + `cmp` 通过；
- 真实 ID header-only 探针记录 WGS/NovaSeq 的约 −99% ID 流变化，并明确不是完整归档；
- `ref_archive_test` 8/8、全量测试、ASan/UBSan、format-check、notes verify、
  `git diff --check` 通过。
