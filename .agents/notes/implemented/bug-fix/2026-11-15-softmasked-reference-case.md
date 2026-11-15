# Agent Note: 参考 FASTA 的 soft-mask 大小写归一化

Status: implemented

## Problem

真实 UCSC hg38 是 soft-masked FASTA，重复区使用小写 `a/c/g/t`。`fqc_ref_archive` 原先
在 `loadReference` 中原样保留大小写，随后把参考切片与大写 read 直接比较，导致小写参考碱基
被误计为 mismatch。真实 WGS R1 头部首轮运行因此只有 109,476/298,974 条 mapped，
归档为 10,258,672 B（9.741× raw），不能作为有效压缩比。

首条 read 的最小证据：`chr2:33031418-33031568` 的 151 bp 参考切片含 46 个
lowercase 碱基；原样比较为 46 个 mismatch，归一化后为 1 个，和 PAF `NM:i:2` 的量级一致。

## Decision

`loadReference` 在构造 contig 序列时把 ASCII `a-z` 统一转为大写：

- 只改内存中的参考表示，不修改用户的 FASTA；
- mismatch 计算、CIGAR 重建和参考摘要都使用归一化序列；
- 对 ACGTN/IUPAC 的语义不变，大小写不再影响映射结果；
- 新增 `RefArchiveTest.NormalizesSoftMaskedReferenceCase`，用小写参考断言 mapped/literal
  计数、`verify` 和 FASTQ round-trip。

## Alternatives considered

- **要求用户先运行 `samtools faidx`/`tr` 转大写**：把工具的正确性责任推给用户，真实
  UCSC/Ensembl 下载默认就是 soft-masked，不可接受。
- **逐碱基做 `toupper` 比较**：每个 M 操作都多一次大小写判断；参考只加载一次，预先归一化
  更简单，也让摘要与解码路径一致。
- **保留原大小写，按大小写不敏感哈希标识参考**：会保留解码时的原始序列，但 mismatch
  计算仍要逐位归一化，且当前格式的摘要语义更复杂；暂不采用。
- **把所有 read 转小写**：会改变参考与 read 的比较路径，还会掩盖 read 侧的非法字符检查；
  不采用。

## Consequences

- 真实 WGS 重跑后 mapped 从 109,476 升到 296,343/298,974，归档 10,258,672 B →
  **4,547,935 B（21.974× raw）**；quality/alignment 三路账才有意义。
- 参考摘要绑定归一化后的 contig 序列；同一参考的大小写差异不会被视为不同输入，符合
  比对坐标的生物学语义。
- 回归测试只在实验工具层；默认 `fqc-sequential/v2` 不受影响。
- 仍不解决 `S/H/N/P`、BAM/CRAM、paired-end、完整 30× WGS 或速度问题。

## Verification

- `RefArchiveTest.NormalizesSoftMaskedReferenceCase` 通过；
- 真实 WGS R1 头部 `verify` 与解压 `cmp` 通过，恢复 FASTQ SHA 与输入一致；
- 两次真实归档逐字节一致，SHA 均为
  `6f50a3146e018c33fffe6be3a21af9163b8ae1223d1b386f6f440507bb3b647a`；
- 详细基线见
  [2026-11-15-real-wgs-srr14724532.md](../../../../docs/max-ratio-baselines/2026-11-15-real-wgs-srr14724532.md)。
