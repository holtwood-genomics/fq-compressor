# Agent Note: Phase 2.1 自适应 ID front coding

Status: implemented

## Problem

Phase 2 的 `fqc-max-ref/v1` 合成归档中，ID 流为 496,574 / 827,688 B，是最大单项。
真实 Illumina WGS 的 read ID 有稳定的 run/flowcell/lane/tile/x/y 结构，zstd 对逐条
长前缀的利用不够；但随机 ID（例如 wgsim）如果强行 front coding 反而会变大。需要在
不增加全局状态、不改变参考/质量编码的前提下，按块选择更优的 ID 表示。

## Decision

在 `fqc-max-ref/v1` 的既有 `idCodec` 字段上增加 `idCodec=2`：

- raw：`varint(count)` + 每条 `varint(idLen)+id+varint(commentLen)+comment`；
- front-coded：每条记录写相对上一条的 id/comment 公共前缀长度 + 后缀长度 + 后缀；
- 编码端同时生成 raw / front 两种逻辑流，各自 zstd19，按块取更小者；
- 尾部 4 字节保留位保存 front 逻辑流的未压缩长度；raw 时写 0，保持旧归档逐字节兼容；
- block checksum 继续覆盖解码后的 raw ID 流；解码端重建 raw 后校验长度和 checksum。

不改变 global magic/version、默认 CLI、参考摘要、PAF/reference 契约或质量 codec。

## Alternatives considered

- **全局强制 front coding**：随机 wgsim ID 上 zstd19 从 495,131 B 变 545,494 B；
  不可接受，必须自适应。
- **数字 token/delta**：对 Illumina ID 可能更强，但需要字段结构识别、数字溢出和
  非标准 ID 回退；先用无语法依赖的 front coding 验证收益。
- **只做真实 ID 探针，不改归档**：无法证明 codec 能落到 `verify`/`cmp` 链路；当前
  改动保留旧格式读取并加入结构化 ID 强制测试，成本可控。
- **新 magic/version**：更干净，但当前是实验工具，且尾部字段可向后兼容扩展；正式格式
  仍会重新冻结，不在本轮制造第二个 magic。

## Consequences

- 收益：真实 WGS/NovaSeq 头部 ID zstd19 约 114,099/111,762 B → 1,085/1,105 B
  （约 −99%）；HiSeq 85,492 B → 83,641 B（−2.2%）。
- 收益：结构化 ID 合成 R1 17,700,000 B → 331,553 B（53.385× raw），`verify` + `cmp`
  通过；原始 wgsim R1 仍选 raw，归档 SHA 与改动前一致。
- 代价：每块多做一次 ID 逻辑流构造和 zstd 压缩；本轮只追求压缩比，不做速度门禁。
- 边界：53.385× 是人工结构化 ID + 合成参考 + 恒定 Q23 的机制验证，不是真实 30× WGS；
  真实 WGS/NovaSeq 的 ID 探针只测 header，不代表完整归档。
- 需要重访：数字 token/delta、字段字典、跨块模型、paired-end 联合 ID 编码、真实
  WGS reference/PAF 归档验收。

## Verification

- `ref_archive_test`：8 个场景，新增结构化 ID 强制 front coding 的 `verify`/`cmp`。
- 原始 wgsim R1 重压缩：`front_id_blocks=0`，归档 SHA
  `23fe0fd1ef9cfef84c60195b6a86b79e31d2159f59f5c5a751d95b61ee42b2d5`，与改动前逐字节一致。
- 结构化 ID R1：331,553 B，53.385× raw，`front_id_blocks=2`，`verify` + `cmp` 通过。
- 详细数据与复现命令见
  [docs/max-ratio-baselines/2026-11-15-id-codec.md](../../../../docs/max-ratio-baselines/2026-11-15-id-codec.md)；
  契约见
  [openspec/changes/2026-11-15-max-ratio-phase2-id-codec/design.md](../../../../openspec/changes/2026-11-15-max-ratio-phase2-id-codec/design.md)。
