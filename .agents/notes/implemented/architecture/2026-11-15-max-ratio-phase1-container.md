# Agent Note: Phase 1 `fqc-max/v1` 实验容器

Status: implemented

## Problem

质量 codec 探针已经证明 fqzcomp 在三个真实样本上稳定优于 zstd19，但探针只在 `/tmp`
里输出 quality 流，无法回答“质量收益能否落成可校验的归档”。同时，设计文档要求 max-ratio
与 `fqc-sequential/v2` 分离；如果直接把 fqzcomp 塞进 v2 帧，会破坏 v2 的帧独立性、兼容性
和“先格式后实现”的边界。

另一个现实约束是 htscodecs 不在 Conan Center。质量探针刻意从外部源码树构建，避免实验
依赖倒逼产品构建；容器阶段必须决定是继续要求外部路径，还是 vendor 一个最小子集。

## Decision

新增实验性 `fqc-max/v1` 归档与三个显式 CLI：

- `max-compress` / `mc`
- `max-decompress` / `mx`
- `max-verify` / `mv`

格式与实现边界：

- 独立 magic `FQCMX1\r\n`、独立全局头/块头/footer，不复用 v2 的 magic 或帧语义；
- ID 与 2-bit packed sequence 复用 v2 的**逻辑流编码**（`encodeFrame` +
  `decodeLogicalStreams`），但外层是独立的 zstd 流与块头；
- 质量流按块在 htscodecs fqzcomp（策略 0/1 自适应）与 zstd19 之间取较小者；
- 块级 XXH64 + 全局滚动校验和；`max-verify` 完整解码，不只读头；
- 输入仍走生产 `FastqParser`，输出 canonical LF FASTQ；不声称支持 `+` 行内容、混合
  行尾或空 read 的逐字节还原；
- 默认 CLI、`fqc-sequential/v2`、fast 流水线行为不变；
- vendor htscodecs `v1.6.7` 的最小 C 子集（`fqzcomp_qual`、`utils` 及相关头），固定
  tag/commit/tarball SHA，保留 BSD-3-Clause/LICENSE；构建为私有静态库
  `fqc_fqzcomp`，不安装、不暴露 C API；Conan `exports_sources` 包含 `third_party/*`。

## Alternatives considered

- **直接改 `fqc-sequential/v2`**：复用现成帧、校验和 e2e 最省事，但会让 v2 解码器依赖
  fqzcomp，破坏旧归档兼容与帧独立性；也违反 ADR-0003 的“独立格式族”决策。
- **继续只做外部源码树工具**：保持产品零依赖，但无法在 CI/用户机器上复现归档，无法验证
  真实文件大小与 `max-verify`；容器阶段需要一个可构建的依赖边界。
- **要求用户提供 htscodecs 源码路径**：避免 vendor，但把可复现性推给用户，且 Conan
  `create`/CI 无法自包含；对一个实验容器而言维护成本更高。
- **只做质量探针，不写容器**：风险最低，但无法回答“归档缩减是否真实”，也无法暴露
  块头、校验和、空归档、截断输入等容器级问题。
- **复用 v2 magic、只加 codec ID**：表面兼容，实际会让旧解码器把新归档误判为 v2 并产生
  难以诊断的错误；独立 magic 让失败模式清晰。
- **一开始就做参考/伪基因组**：压缩比上限更高，但会把 Phase 1 的质量/容器验证与 Phase 2/3
  的 mapper、内存、spill 风险混在一起；先冻结一个可校验的 Phase 1 容器更可控。

## Consequences

- 收益：质量 codec 收益从“估算”变成实际归档；HiSeq 1,285,285 B（相对 v2 −21.36%，
  5.087× raw），NovaSeq 头部 12,280,644 B（−24.87%，8.165× raw），WGS 头部
  12,828,169 B（−23.69%，7.790× raw）；三个归档 `max-verify` 与 `cmp` 均通过。
- 代价：产品构建新增 C 语言与 vendored 第三方代码；fqzcomp C 源码的警告被显式关闭，
  上游升级需要重新核对 tag/许可证/构建；`--zstd-level 19` 压缩明显慢于 fast 路径。
- 边界：这是 Phase 1 原型，不是最终 max-ratio。canonical LF 之外的字节保真、paired-end
  联合、完整 35× WGS、`--max-ratio` 顶层开关都未实现；相对 v2 的缩减混入了 ID/sequence
  的 zstd19 收益，不能全部归因于质量 codec。后续 Phase 2 已用 PAF + external reference
  原型验证参考编码，但仍是合成机制验证，见
  [Phase 2 笔记](2026-11-15-max-ratio-phase2-ref.md)。
- 需要重访：如果后续格式契约要求 `+` 行/CRLF/空 read 逐字节保真，当前 `FastqParser` +
  canonical 输出路径必须替换；如果 fqzcomp 在完整 run 上不再稳定胜出，保留 per-block
  zstd 回退并重新评估是否继续 vendor。
- 验证：当前全量 `20/20` tests（含 `max_archive_test`、`ref_archive_test` 与 e2e）、
  `format-check`、`notes verify`、`git diff --check`；Phase 1 当时记录为 19/19，Phase 2
  加入 `ref_archive_test` 后为 20/20。详细命令、输入/输出 SHA 与口径见
  [docs/max-ratio-baselines/2026-11-15-max-container.md](../../../../docs/max-ratio-baselines/2026-11-15-max-container.md)
  与 [格式契约](../../../../openspec/changes/2026-11-15-max-ratio-phase1-container/design.md)。
