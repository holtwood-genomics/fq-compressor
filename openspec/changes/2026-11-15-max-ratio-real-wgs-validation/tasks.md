# Tasks: max-ratio-real-wgs-validation

## 1. Input and alignment

- [x] 1.1 固定 R1 头部 gzip、FASTQ 与 SHA
- [x] 1.2 固定完整 hg38 FASTA/gzip 与 SHA
- [x] 1.3 生成 minimap2 2.31 primary PAF 并固定 SHA
- [x] 1.4 统计 CIGAR、正负链和 NM 分布

## 2. Archive

- [x] 2.1 运行 `fqc_ref_archive compress`
- [x] 2.2 `verify` + `decompress` + `cmp`
- [x] 2.3 重复压缩并比较归档 SHA
- [x] 2.4 记录 ID/quality/alignment 三路字节账

## 3. Fix and regression

- [x] 3.1 修复 soft-masked 参考大小写误判
- [x] 3.2 新增 `NormalizesSoftMaskedReferenceCase` 回归测试
- [x] 3.3 记录修复前 109,476 mapped / 9.741× 的诊断值

## 4. Comparison and docs

- [x] 4.1 gzip -6 / zstd -19 / FQC v2 / CRAM 对照
- [x] 4.2 真实基线与 datasets manifest
- [x] 4.3 一页纸总结和复现脚本
- [x] 4.4 CHANGELOG、README、算法/架构/roadmap/OpenSpec 同步

## 5. Explicit non-goals

- [ ] 5.1 完整 35× run
- [ ] 5.2 paired-end 联合
- [ ] 5.3 BAM/CRAM sidecar、内置 mapper
- [ ] 5.4 S/H/N/P、长读 spliced alignment
- [ ] 5.5 速度/内存/正式格式冻结
