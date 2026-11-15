# Tasks: max-ratio-phase2-cigar

## 1. Codec

- [x] 1.1 解析 PAF `cg:Z:` 的 M/=/X/I/D，拒绝 S/H/N/P 与零长度/超限操作
- [x] 1.2 query/target 消费量与 PAF 坐标一致性校验
- [x] 1.3 扩展 flags bit3、query 范围、CIGAR、insertion、clip、mismatch 布局
- [x] 1.4 正/负链 aligned query 重建与左右 clip 还原
- [x] 1.5 纯全长 `<len>M` 保持旧布局
- [x] 1.6 `clipped` / `indel` 统计与 CLI 输出

## 2. Correctness and rejection

- [x] 2.1 正链左右 clip
- [x] 2.2 正链 I/D
- [x] 2.3 负链 I 与负链 clip
- [x] 2.4 旧 Phase 2 归档读取兼容
- [x] 2.5 截断/尾部字节/坐标/长度/操作数拒绝路径保持

## 3. Measurements

- [x] 3.1 R1/R2 重新压缩、verify、decompress + cmp
- [x] 3.2 R1 重复压缩逐字节一致
- [x] 3.3 结构化 ID R1 在新 CIGAR codec 下重测
- [x] 3.4 记录输入/归档 SHA、字节账、mapped/literal/clipped/indel

## 4. Documentation and gates

- [x] 4.1 `docs/max-ratio-baselines/2026-11-15-ref-cigar.md`
- [x] 4.2 本 proposal/design/tasks/verification
- [x] 4.3 更新工具 README、max-ratio 设计、Phase 2 基线、CHANGELOG、roadmap
- [x] 4.4 implemented Agent Note
- [x] 4.5 build、全量测试、ASan/UBSan、format-check、notes verify、`git diff --check`

## 5. Explicit non-goals

- [ ] 5.1 S/H/N/P 与 spliced alignment
- [ ] 5.2 BAM/CRAM sidecar、内置 mapper
- [ ] 5.3 paired-end 联合
- [ ] 5.4 参考嵌入与 SHA-256
- [ ] 5.5 真实 30× WGS 与 ORA/Genozip 同数据验收
- [ ] 5.6 原子输出、内存上限、并发与速度门禁
