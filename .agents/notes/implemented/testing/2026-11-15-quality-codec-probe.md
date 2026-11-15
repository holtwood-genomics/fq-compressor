# Agent Note: Phase 1 质量 codec 探针

Status: implemented

## Problem

Phase 0 已经量出真实 HiSeq 样本的质量流是当前归档最大的一块，但设计里对质量 codec 只有
假设：fqzcomp 可能赢全质量 HiSeq，NovaSeq 分箱质量可能让 zstd 赢。没有本地实现证据时，
无法判断是否值得进入 Phase 1 容器，也无法确定块大小与策略选择。

同时，htscodecs 不在 Conan Center，直接把它加入产品依赖会把“实验验证”和“生产构建”绑在
一起；而在 `/tmp` 里写一次性 harness 又不可追溯。

## Decision

新增开发专用 `tools/quality_probe`：

- 只解析 FASTQ 的 quality 流，支持 LF/CRLF、空 read、literal `+` 行；quality 非
  Phred+33 可打印字节时 fail closed；
- 调用 htscodecs `v1.6.7` 的 `fqz_compress`/`fqz_decompress`，逐块验证 quality 值
  与每条长度 round-trip；
- 每块可固定策略 0–3，或自适应尝试 0/1 并取较小者；
- 用 zstd 19 对同一 quality 二进制流做对照；
- 输出文本或 `fqc-quality-probe/v1` JSON；
- 通过 `tools/quality_probe/build.sh` 从外部 htscodecs 源码树构建，不进入产品 CMake
  目标、不安装、不改变 `fqc-sequential/v2` 或默认 CLI。

固定上游身份：tag `v1.6.7`、peeled commit
`b9fc194f772e45bb0a1f44b08cbf8697a1384bae`、tarball SHA-256
`4f36d2800e97468435f724a4a0b26dfda6344b11dc16d3fa50b03f47b862e15a`；
许可证为 BSD-3-Clause（rANS/range coder 文件为 Public Domain/CC0）。

实测结论：

- HiSeq 全质量 2.525M bases：fqzcomp 558,189 B vs zstd19 678,308 B（−17.7%）；
- NovaSeq RNA-Seq 分箱质量头部 45.5415M bases：fqzcomp 2,159,788 B vs zstd19
  2,802,326 B（−22.9%）；
- WGS 分箱质量头部 45.1451M bases：fqzcomp 2,168,649 B vs zstd19 2,823,235 B
  （−23.2%）；
- 按当前 FQC 归档估算，质量流替换可带来 15.5%–16.6% 总缩减；但这是估算，不是已实现
  归档；
- 块大小必须按 bases 而不是 read 条数：HiSeq 1k reads/块比整文件差 30.1%，建议从
  4M bases/块起步；
- 策略 0/1 自适应足够；策略 2/3 在已测样本上都更差。

完整数据、分块曲线、输入/输出 SHA 与未验证边界见
[docs/max-ratio-baselines/2026-11-15-quality-codecs.md](../../../../docs/max-ratio-baselines/2026-11-15-quality-codecs.md)。

## Alternatives considered

- **直接加入产品 CMake/Conan**：能获得 CI 覆盖，但 htscodecs 不在 Conan Center，且
  Phase 1 容器格式尚未冻结；先把实验工具与产品构建隔离，避免依赖决策被工具倒逼。
- **继续用 `/tmp` C harness**：开发最快，但源码、编译命令和上游身份不可追溯；本轮把
  harness 收敛为仓库内工具，并保留外部源码路径构建。
- **在 `fqc_ratio_probe` 里直接加 fqzcomp**：ratio probe 的职责是字节计量，不应依赖
  第三方质量编码器；独立工具让失败面更小，也便于单独删除。
- **只测整文件一个块**：会掩盖块大小对模型 warmup 的影响；本轮补了 reads/base 分块曲线，
  直接把“块目标按 bases”写成设计约束。
- **用 rANS 静态模型作主对照**：质量字母表/位置上下文更适合 fqzcomp；rANS 仍需后续
  评估，但不应在没有证据时占默认候选位。

## Consequences

- 收益：Phase 1 的质量门禁从假设变成可复现实验；fqzcomp 在两个质量 regime 上都稳定赢
  zstd19，且输出确定、round-trip 通过；块大小与策略选择有了本地依据。
- 代价：工具依赖外部 htscodecs 源码树与 Conan zstd；默认构建不编译它，CI 不会自动覆盖。
  这是有意的隔离，但也意味着后续集成时必须另写构建/格式契约。`scripts/lint.sh` 的
  clang-tidy 文件集显式排除该目录（它不在 CMake compile_commands 中），格式检查仍覆盖。
- 边界：NovaSeq 是 RNA-Seq 头部子集，WGS 是约 300k reads 头部子集，都不是完整 run；
  没有测 paired-end read2/reverse、完整 run、吞吐/内存；归档 −15.5%～−16.6% 只是
  质量流替换估算。
- 验证：`fqc_quality_probe` 在 HiSeq/NovaSeq 上 round-trip 通过；重复运行压缩输出
  SHA 一致；`tools/quality_probe/README.md` 记录固定上游与构建命令。
