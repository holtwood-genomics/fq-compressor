# Proposal: max-ratio Phase 2 参考压缩原型

- 日期：2026-11-15
- 状态：实验原型已实现，待用户复核；不是最终对外格式
- 关联：[docs/max-ratio-design.md](../../../docs/max-ratio-design.md)、
  [ADR-0003](../../../docs/decisions/0003-max-ratio-mode.md)、
  [Phase 2 实测](../../../docs/max-ratio-baselines/2026-11-15-ref-container.md)

## 为什么

Phase 1 的 `fqc-max/v1` 只解决了质量 codec 与容器，序列仍是 2-bit + zstd；真实 HiSeq
小样本约 5.09× raw、NovaSeq/WGS 头部约 7.8–8.2× raw。设计与业界调研都表明，绝对最高
压缩比路线是**有参压缩**：参考位置 + 方向 + mismatch/clip 差异 + 质量上下文。ORA /
Genozip 的公开量级约 20–25× raw、5–6× gzip，差距来自参考与专用模型，不是 zstd 档位。

因此需要一个独立的 Phase 2 实验，回答：

1. 参考 + PAF 位置/差异编码能否在真实工程中无损 round-trip；
2. 参考摘要/contig 表/块校验是否 fail closed；
3. 归档体积是否真的由“序列”转为“ID + 质量 + alignment 描述”；
4. 哪些边界必须在正式 `max-ref` 之前补齐。

## 做什么

新增**开发专用** `fqc_ref_archive`（不安装、不进入主 `fqc` CLI）：

- `compress`：FASTQ + external reference FASTA + PAF sidecar → `fqc-max-ref/v1`；
- `decompress`：同一 reference + archive → canonical LF FASTQ；
- `verify`：完整解码所有块、校验块/全局 checksum，不写 FASTQ；
- 只接受 PAF 中全长、纯 `<len>M`、无剪切的比对；其余 read 回退 literal；
- 质量流每块在 htscodecs fqzcomp 策略 0/1 与 zstd19 之间取较小者；
- ID/alignment/literal 流用 zstd19；
- 块级 XXH64 + footer 滚动全局校验和；参考摘要/contig 表不匹配 fail closed；
- 新增 7 个 `ref_archive_test` 场景与合成数据实测（Phase 2.1 ID codec 扩展后为 8 个）。

## 不做什么

- **不是**最终 `--max-ratio` CLI，不改变 `fqc-sequential/v2` 或默认命令；
- 不内置 mapper，不支持 BAM/CRAM sidecar；
- 不支持 clip/indel 编码、参考嵌入、paired-end 联合；
- 不承诺 `+` 行内容、CRLF/混合行尾、空 read 的逐字节保真（仍走生产 parser）；
- 不做速度/内存门禁、并发、原子输出；
- 参考摘要仍用 XXH64，不是最终 SHA-256 口径。

## 验收

合成 5 Mb 随机参考 + wgsim 3× 双端 150 bp：

- R1：17,423,235 B → 827,688 B（21.050× raw），mapped 49,194 / literal 806；
- R2：17,423,235 B → 827,546 B（21.054× raw），mapped 49,211 / literal 789；
- `verify` 与解压后 `cmp` 均通过；重复压缩 SHA 一致；
- 空输入、截断、footer 尾部多余字节、参考摘要不匹配、输出路径别名均 fail closed。

**边界**：参考不计入归档；合成质量恒定 Q23；数据不是人类 30× WGS；21× 不能外推为
产品压缩比，也不等于 ORA/Genozip 的 5–6× gzip。
