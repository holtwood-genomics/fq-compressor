# Proposal: 真实 WGS 参考归档小切片验收

- 日期：2026-11-15
- 状态：已完成实验验收；不是新格式契约
- 基础：`fqc-max-ref/v1` Phase 2.2 CIGAR
- 证据：[真实 WGS 基线](../../../docs/max-ratio-baselines/2026-11-15-real-wgs-srr14724532.md)

## 为什么

Phase 2/2.2 此前只有 5 Mb 合成参考 + wgsim 数据，无法回答真实 ID/quality/CIGAR 分布下
参考归档是否仍然成立。需要一份可复现的真实 WGS 小切片，验证：

- 真实参考（完整 GRCh38）和 minimap2 PAF；
- 正/负链、clip/indel、分箱质量、真实 ID；
- `verify`/解压 `cmp` 和重复压缩确定性；
- 与 gzip/zstd/FQC v2/CRAM 的字节账和口径边界。

## 做什么

- 取 `SRR14724532` R1 gzip 头部，裁剪为 298,974 条完整 reads；
- 用完整 UCSC hg38 和 minimap2 2.31 primary PAF；
- 运行 `fqc_ref_archive compress/verify/decompress`，并做 `cmp`；
- 记录 ID/quality/alignment 三路字节账、归档 SHA、重复压缩 SHA；
- 生成 gzip -6、zstd -19、FQC v2、CRAM 对照；
- 修复真实 hg38 soft-mask 小写参考导致的大小写误判，并加回归测试；
- 增加可复现脚本和一页纸摘要。

## 不做什么

- 不下载完整 35× run；
- 不做 paired-end 联合压缩；
- 不实现 BAM/CRAM sidecar、内置 mapper、S/H/N/P 或长读 spliced alignment；
- 不把 external reference/PAF 计入主口径；
- 不把 R1 头部结果写成完整 30× 产品结论；
- 不做速度/内存优化。

## 验收

- 归档 4,547,935 B（21.974× raw），mapped 296,343/298,974；
- `verify` + `cmp` 通过，恢复 FASTQ SHA 与输入一致；
- 重复压缩逐字节一致；
- `ref_archive_test` 增加 soft-mask 回归并通过；
- 文档记录 gzip/zstd/FQC/CRAM 对照和参考/PAF 计入/不计入口径。
