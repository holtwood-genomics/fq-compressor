# Proposal: max-ratio Phase 2.2 clip/indel CIGAR 编码

- 日期：2026-11-15
- 状态：实验扩展已实现，待复核；不是最终对外格式
- 基础：[Phase 2 参考原型](../2026-11-15-max-ratio-phase2-ref/design.md)
- 前序：[Phase 2.1 ID codec](../2026-11-15-max-ratio-phase2-id-codec/design.md)
- 基线：[2026-11-15-ref-cigar.md](../../../docs/max-ratio-baselines/2026-11-15-ref-cigar.md)

## 为什么

Phase 2 原型只接受 `queryStart=0`、`queryEnd=readLength`、target span=readLength 的纯
`<len>M` 比对。真实 wgsim + minimap2 输出中已有：

- 短 query 的 `145M` 等 soft-clipped 比对；
- `90M1D60M`、`24M1I125M` 等短 indel；
- 负链上的同类 CIGAR。

这些 read 目前全部回退 literal。R1/R2 各约 800 条，既浪费序列字节，也让 mapped 率
被低估。先在 PAF 原型中把 CIGAR 变成显式编辑脚本，验证无损重建和失败关闭，再考虑
BAM/内置 mapper。

## 做什么

- 解析 `M/=/X/I/D`；`=`/`X` 与 `M` 一样由参考和 read 重新计算实际差异；
- 支持 PAF query 范围短于整条 read 的左右 clip，clip 序列按 base code 存在 alignment 流；
- 支持 I（query 多出的碱基）和 D（参考多出的碱基）；D 不存序列；
- 新增 per-read flags bit3：只有 clip 或 I/D 的 mapped read 使用扩展布局；
- 纯全长 `<len>M` 保持旧布局，保持旧归档可读和旧数据路径不变；
- 负链先在 reverse-complement 后的 aligned query 上执行 CIGAR，再反向还原；
- `--max-mismatches` 仍只限制 `M/=/X` 内的替换；CIGAR 操作数上限 256；
- 新增 `clipped` / `indel` 统计，写入 CLI 输出和测试。
- `S/H/N/P`、空 CIGAR、零长度、坐标/消费量不一致时回退 literal，不静默部分解码。

## 不做什么

- 不改变 `fqc-max-ref/v1` 的 magic/global header/block header/footer；
- 不实现 BAM/CRAM sidecar 或内置 mapper；
- 不支持 `S/H/N/P`、长读 spliced alignment、CIGAR 的 base-level 语义校验；
- 不做 paired-end 联合、参考嵌入、SHA-256、原子输出、内存上限或速度门禁；
- 不把 5 Mb 合成数据结果写成人类 30× WGS 结论。

## 验收

- `ref_archive_test` 新增 clip/indel/负链 CIGAR round-trip，并覆盖统计计数；
- 旧 Phase 2 R1 归档（827,688 B）仍可由新解码器 `verify`；
- 新 R1：17,423,235 B → 792,217 B（21.993× raw），mapped 49,998 / literal 2，
  clipped 51 / indel 753；`verify` + `cmp` 通过；
- 新 R2：17,423,235 B → 796,633 B（21.871× raw），mapped 50,000 / literal 0，
  clipped 44 / indel 745；`verify` + `cmp` 通过；
- 结构化 ID R1：296,082 B（59.781× raw），`verify` + `cmp` 通过；
- 全量 build/test、ASan、format-check、notes verify、`git diff --check` 通过。
