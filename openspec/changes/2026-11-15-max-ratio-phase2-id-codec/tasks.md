# Tasks: max-ratio-phase2-id-codec

## 1. Codec

- [x] 1.1 raw / front-coded 两种 ID 逻辑流
- [x] 1.2 每块自适选择与 `codecIdsSize` 尾部字段
- [x] 1.3 front-coded 解码、长度/边界校验、raw 流重建
- [x] 1.4 block checksum 继续覆盖 raw ID 流
- [x] 1.5 `front_id_blocks` 统计与 CLI 输出

## 2. Tests

- [x] 2.1 结构化 ID 强制 front coding 的 `verify`/`cmp`
- [x] 2.2 原有 mapped/literal、参考摘要、截断、多块、空归档、尾部字节、路径别名测试
- [x] 2.3 旧 raw 归档可验证/解压
- [x] 2.4 原始 wgsim 归档逐字节不变回归

## 3. Measurements

- [x] 3.1 HiSeq/WGS/NovaSeq 真实 header-only ID 探针
- [x] 3.2 随机 wgsim ID 的 raw-wins 回归
- [x] 3.3 结构化 ID 合成归档：331,553 B / 53.385× raw
- [x] 3.4 输入/PAF/归档 SHA、流字节、mapped/literal、verify/cmp 记录

## 4. Documentation and gates

- [x] 4.1 `docs/max-ratio-baselines/2026-11-15-id-codec.md`
- [x] 4.2 本 proposal/design/tasks/verification
- [x] 4.3 更新 Phase 2 基线、工具 README、max-ratio 设计、CHANGELOG
- [x] 4.4 实现笔记
- [x] 4.5 build、全量测试、format-check、notes verify、`git diff --check`

## 5. Explicit non-goals

- [ ] 5.1 数字 token/delta 与字段字典
- [ ] 5.2 跨块 ID 模型
- [ ] 5.3 paired-end 联合 ID 编码
- [ ] 5.4 真实 30× WGS reference/PAF 归档验收
- [ ] 5.5 正式格式冻结与 SHA-256 参考身份
