# Tasks: max-ratio-phase2-ref

## 1. Prototype format and engine

- [x] 1.1 定义 `fqc-max-ref/v1` global header / contig table / block / footer
- [x] 1.2 external reference FASTA 加载、contig 索引与 XXH64 摘要
- [x] 1.3 PAF 解析、primary/MAPQ 选择、纯 `<len>M` 全长比对
- [x] 1.4 mapped 正/负链、mismatch delta/alt、literal 回退
- [x] 1.5 ID/quality/alignment 三路载荷与块级/全局 checksum
- [x] 1.6 `compress` / `decompress` / `verify` CLI

## 2. Correctness and rejection

- [x] 2.1 参考摘要/contig 表不匹配 fail closed
- [x] 2.2 截断归档、footer 尾随字节、空归档
- [x] 2.3 多块 round-trip 与 `--max-mismatches` 回退
- [x] 2.4 输出路径别名输入/参考拒绝
- [x] 2.5 mapped/literal/长度/坐标/mismatch 顺序校验

## 3. Tests and measurements

- [x] 3.1 `ref_archive_test` 7 个场景（Phase 2.1 后 8 个）
- [x] 3.2 5 Mb 合成参考 + wgsim 3× 双端 150 bp 实测
- [x] 3.3 R1/R2 `verify` + 解压 `cmp`；重复压缩 SHA 一致
- [x] 3.4 输入/归档 SHA、流字节、mapped/literal、时间/RSS 记录

## 4. Documentation and gates

- [x] 4.1 `tools/ref_archive/README.md`
- [x] 4.2 `docs/max-ratio-baselines/2026-11-15-ref-container.md`
- [x] 4.3 本 OpenSpec proposal/design/tasks/verification
- [x] 4.4 implemented Agent Note
- [x] 4.5 更新 max-ratio 设计、roadmap、README、ALGORITHM、ARCHITECTURE、CHANGELOG
- [x] 4.6 build、全量测试、format-check、notes verify、`git diff --check`

## 5. Explicit non-goals

- [ ] 5.1 内置 mapper / BAM sidecar
- [ ] 5.2 clip/indel 编码
- [ ] 5.3 参考嵌入与 SHA-256
- [ ] 5.4 paired-end 联合
- [ ] 5.5 真实 30× 人类 WGS 验收
- [ ] 5.6 原子输出、内存上限、并发与速度门禁
