# Design: 真实 WGS 参考归档小切片

## 1. 数据

- reads：`SRR14724532` R1 gzip 前 21,110,784 B，裁剪为 298,974 条完整 4 行记录；
- reference：完整 UCSC hg38 `hg38.fa`，external reference，不计入归档；
- PAF：minimap2 2.31-r1302，`-x sr -c --secondary=no`；
- quality：NovaSeq 分箱质量，本切片 4 个符号。

## 2. 编码

沿用 `fqc-max-ref/v1`：

```text
FASTQ + PAF + external FASTA
  -> ID front coding / quality fqzcomp / alignment CIGAR + zstd19
  -> fqmr
```

不改变 magic、block header、footer 或 codec 选择逻辑。参考 FASTA 在 `loadReference`
加载时把 soft-mask 小写 `a-z` 统一成大写；mismatch、CIGAR 重建和参考摘要都使用归一化
序列。

## 3. 验收

- `compress` 参数：`--block-bases 4000000 --max-mismatches 16`；
- `verify` 与 `decompress` 后 `cmp`；
- 两次压缩比较归档 SHA；
- 三路体积、mapped/literal/clipped/indel 和 PAF CIGAR 统计；
- gzip -6 / zstd -19 / FQC v2 / CRAM 仅作大小参考，明确功能不等价。

## 4. 口径

主口径是 external reference + external PAF，不计入归档。小切片不摊销 3.27 GB 参考；
若计入参考得到 0.0305×，若计入 PAF 得到 1.865×。完整 30× run 必须另做完整参考摊销、
paired-end 和外部工具同数据对比。

## 5. 不包含

- 完整 35× run；
- paired-end、BAM/CRAM sidecar、内置 mapper；
- S/H/N/P、长读 spliced alignment；
- 参考嵌入、SHA-256、正式格式、原子输出或速度门禁。
