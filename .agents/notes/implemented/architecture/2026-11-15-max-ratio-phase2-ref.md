# Agent Note: Phase 2 `fqc-max-ref/v1` 参考压缩原型

Status: implemented

## Problem

Phase 1 的 `fqc-max/v1` 只证明质量 codec 与容器收益：真实样本 5.087×–8.165× raw，
序列仍是 2-bit + zstd。设计与业界调研都指向有参压缩是绝对最高压缩比路线，但仓库里
没有可执行的参考编码原型，无法回答以下工程问题：

- reference + PAF 的位置/方向/mismatch 编码能否无损还原？
- 参考摘要/contig 表/块校验能否在错误参考、截断、尾随字节下 fail closed？
- 归档体积会从“序列”转移到哪些流？
- 正式 `max-ref` 之前必须补齐哪些边界？

## Decision

新增**开发专用** `fqc_ref_archive`，实现 `fqc-max-ref/v1` 实验格式：

- `compress`：FASTQ + external reference FASTA + PAF sidecar → archive；
- `decompress`：同一 reference + archive → canonical LF FASTQ；
- `verify`：完整解码并校验块/footer，不写 FASTQ；
- PAF 只接受 `queryStart=0`、`queryEnd=readLength`、target span=readLength、
  `cg:Z:<digits>M` 的全长线性比对；其余 read 回退 literal；
- mapped 记录存 contig id、target offset、orientation、mismatch delta/alt；负链先
  reverse complement 再比较；
- ID/alignment/literal 流 zstd19；quality 每块 fqzcomp 策略 0/1 vs zstd19 取较小者；
- 块级 XXH64 覆盖 ids/qualities/alignments 原始流，footer 存滚动全局校验和；
- 参考身份用 contig 名/长度/序列的 XXH64 摘要（原型限制；最终要 SHA-256）；
- 新增 7 个 `ref_archive_test` 场景与合成数据基线（Phase 2.1 后为 8 个）。

不改变 `fqc-sequential/v2`、默认 CLI 或 Phase 1 的 `max-compress`。

## Alternatives considered

- **直接内置 mapper**：能减少外部 PAF 依赖，但会把 seed/index/重复区域/候选选择与
  归档格式一起引入；当前先验证位置/差异编码本身，PAF 是最小可替换接口。
- **BAM/CRAM sidecar**：更贴近生产，但 BAM 不保留原始 `+` 行/顺序，且需要 name-sort/
  索引与 CIGAR 解码；先做 PAF，不承诺 BAM-only。
- **支持 clip/indel**：能提高真实数据 mapped 率，但需要完整的 CIGAR 编辑脚本与边界
  测试；当前纯 M 映射已能验证结构，先不做。
- **嵌入参考**：可自包含，但参考体积必须计入归档；先做 external reference，避免把
  “参考不计入”与“参考嵌入”两套口径混在一起。
- **直接把参考编码塞进 `fqc-max/v1`**：复用容器会破坏 Phase 1 的独立格式/校验边界，
  且参考摘要/contig 表/alignment 流需要不同语义；独立 magic 让失败模式清晰。
- **只做文档不写原型**：无法验证 reference/PAF/校验链路，也无法暴露 ID/quality/
  alignment 的真实字节结构；实验原型值得保留。

## Consequences

- 收益：合成 5 Mb 参考 + wgsim 3× 双端 150 bp 上，R1 17,423,235 B → 827,688 B
  （21.050× raw），R2 → 827,546 B（21.054× raw）；`verify` + `cmp` 通过；重复压缩
  SHA 一致。证明参考位置/差异编码与外部参考校验链路可工作。
- 收益：归档字节结构变得可解释：R1 的 ID 496,574 B、alignment 330,759 B、quality
  仅 74 B；后续优化目标明确（数字 ID token/delta、真实质量、indel/clip）。
- 代价：新增一个开发工具与格式族；PAF/reference 是压缩输入，解压仍需同一 reference；
  PAF 不计入归档，参考也不计入；两者必须在报告中单列。
- 边界：合成数据、3× 覆盖、恒定 Q23、随机参考，**不能**外推成人类 30× WGS 的 20×
  结论；当前 mapped 率是纯 M 比对的 mapped 率，真实 indel/clip 会降低。
- 边界：仍走生产 `FastqParser`，只保证 canonical LF FASTQ；`+` 行内容/CRLF/空 read
  未保真。没有原子输出、内存上限、并发、paired-end 联合。
- 需要重访：真实 30× WGS 的 mapped 率/质量/ID 结构；参考嵌入与 SHA-256；clip/indel；
  数字 ID token/delta；是否把 PAF 换成内置 mapper 或 BAM sidecar。

## Verification

- `ref_archive_test`：Phase 2 时 7 个场景（mapped/literal、参考摘要、截断、多块/
  mismatch 限制、空归档、footer 尾随字节、输出路径别名）；Phase 2.1 增加结构化 ID
  front coding 后为 8 个。
- 合成 R1/R2：compress → verify → decompress → `cmp`；重复压缩 SHA 一致。
- 详细命令、输入/归档 SHA、流字节与不可外推边界见
  [docs/max-ratio-baselines/2026-11-15-ref-container.md](../../../../docs/max-ratio-baselines/2026-11-15-ref-container.md)。
- 格式契约见
  [openspec/changes/2026-11-15-max-ratio-phase2-ref/design.md](../../../../openspec/changes/2026-11-15-max-ratio-phase2-ref/design.md)。
