# Proposal: paired-end 参考归档小切片

- 日期：2026-11-15
- 状态：已完成实验验收；不是正式格式冻结
- 基础：`fqc-max-ref/v1` Phase 2.2 CIGAR
- 证据：[真实 WGS paired 基线](../../../docs/max-ratio-baselines/2026-11-15-real-wgs-paired.md)

## 为什么

此前 `fqc_ref_archive` 只支持单端：R1/R2 必须分别归档，无法利用 mate 对之间的位置关系。
真实 WGS 是 paired-end；如果同一 contig 上的 R2 仍重复存 contig id 和绝对坐标，会浪费
alignment 字节。需要先验证一个最小 paired 扩展是否能在真实数据上稳定获得收益。

## 做什么

- 增加 `compress --mate R2 --mate-paf R2.paf`，锁步解析 R1/R2；
- 校验 read ID 配对，交错写入同一个 block；
- 全局头 flags bit0 标记 paired；
- 同 contig 的 R2 使用相对 R1 的 zigzag `targetStart` delta，否则保持绝对坐标；
- 增加 `decompress-paired --r1/--r2`，分别恢复两份 FASTQ；
- 旧单端归档保持可读；
- 在 `SRR14724532` R1/R2 各 298,974 reads + 完整 hg38 上验收。

## 不做什么

- 不做跨 mate 共享 ID、联合质量上下文或 insert-size 质量模型；
- 不做完整 30× run、BAM/CRAM sidecar、内置 mapper；
- 不做 `S/H/N/P`、长读 spliced alignment；
- 不把 external reference/PAF 计入主口径；
- 不冻结正式格式，不做速度/内存门禁。

## 验收

- paired 归档 < 两个单端归档之和；
- `verify`、`decompress-paired`、R1/R2 `cmp` 通过；
- 重复压缩逐字节一致；
- 旧单端归档 `verify` 仍通过；
- 新增 paired 单测和真实数据证据。
