# Agent Note: Phase 2.2 clip/indel CIGAR 编码

Status: implemented

## Problem

Phase 2 的 `fqc-max-ref/v1` 只接受全长纯 `<len>M`。真实 wgsim/minimap2 数据里已经有
短 query 的 soft clip（例如 `145M`）和 `I/D`（例如 `90M1D60M`、`24M1I125M`），这些
read 全部回退 literal。合成 R1/R2 各有 806/789 条 literal，其中绝大多数并非真正没有
比对，而是被 CIGAR 限制拒掉；这会同时低估 mapped 率和最大压缩比。

如果直接把 CIGAR 字符串塞进归档，解码端仍然无法知道 insertion 的碱基；如果只支持
soft clip 不支持 indel，真实数据仍会留下大量 literal。因此需要一套可校验的编辑脚本，
但必须保持纯 M 旧布局和旧归档兼容。

## Decision

在 `fqc-max-ref/v1` 的 per-read flags 上加 bit3 `EXTENDED`，只在 clip 或 `I/D` 时使用
扩展布局：

- 解析 `M/=/X/I/D`；`=`/`X` 归并为 M 语义，实际替换由参考与 read 重新计算；
- 扩展记录存 `queryStart/queryEnd`、操作表、I 的 base code、原始方向左右 clip、
  aligned-query 内的 mismatch delta/alt；
- `D` 不存序列；`I` 的碱基按 base code 存；
- 负链先在 reverse-complement 后的 aligned query 上执行 CIGAR，解码再反向还原；
- 纯全长单 M 继续走旧布局，旧归档可读；
- `S/H/N/P`、空/零长度/超限操作、坐标消费量不一致全部回退 literal；
- `--max-mismatches` 仍只限制 M 内的替换，CIGAR 操作数上限 256；
- 新增 `clipped` / `indel` 统计。

## Alternatives considered

- **把 cg:Z 字符串原样存进 alignment 流**：实现最省事，但解码仍缺 insertion 碱基，
  且无法在格式层拒绝 S/H/N/P 或坐标不一致；不采用。
- **只支持 soft clip，不支持 I/D**：可以覆盖短 query 的 145M/147M，但 wgsim 数据中
  仍有约 750 条 indel 会回退 literal，收益不完整。
- **全部 read 都改成扩展布局**：逻辑统一，但会改变旧纯 M 归档字节、增加 2 个 clip
  长度字段的常数开销；保留旧布局更利于兼容和回归。
- **用 BAM/CRAM CIGAR 代替 PAF**：BAM 更接近生产，但会引入 name-sort/索引/原始
  `+` 行保真问题；当前先用 PAF 验证 CIGAR 重建本身。
- **支持 S/H/N/P**：PAF 的 minimap2 `cg` 在当前短读样本中没有 S/H/N/P；盲支持会增加
  坐标语义分支，先明确回退。

## Consequences

- 收益：R1 827,688 → 792,217 B（21.993× raw），R2 827,546 → 796,633 B（21.871× raw）；
  alignment 流分别 −10.72% / −9.36%；mapped 率接近 100%。
- 收益：结构化 ID R1 331,553 → 296,082 B（59.781× raw）；ID/quality 流不变，说明收益
  来自把 literal 重新编码，而不是调 zstd 或质量模型。
- 代价：解码多了 CIGAR 重建、insertion/clip 边界和负链方向分支；只追求压缩比，不做
  速度门禁。
- 边界：仍只支持短读、PAF、external reference；不支持 S/H/N/P、长读 spliced alignment、
  BAM/CRAM、参考嵌入、SHA-256、真实 30× WGS 验收。后续 Phase 2.3 paired 容器与
  R2 坐标 delta 见
  [2026-11-15-max-ratio-paired-end.md](2026-11-15-max-ratio-paired-end.md)。
- 兼容性：新解码器可读旧归档；旧工具会拒绝带 bit3 的新归档，是实验工具的单向扩展。

## Verification

- `ref_archive_test` 新增 `RoundTripsClippedAndIndelReads`：正链左/右 clip、正链 I/D、
  负链 I、负链 clip，共 6 条 read；断言 mapped=6、clipped=3、indel=3，并完成
  `verify` + `cmp`。
- R1/R2 `compress → verify → decompress → cmp` 通过；R1 重复压缩逐字节一致。
- 旧 Phase 2 R1 归档（827,688 B）与结构化 ID 归档（331,553 B）仍可 `verify`。
- 详细数据、SHA 和不可外推边界见
  [docs/max-ratio-baselines/2026-11-15-ref-cigar.md](../../../../docs/max-ratio-baselines/2026-11-15-ref-cigar.md)；
  契约见
  [openspec/changes/2026-11-15-max-ratio-phase2-cigar/design.md](../../../../openspec/changes/2026-11-15-max-ratio-phase2-cigar/design.md)。
