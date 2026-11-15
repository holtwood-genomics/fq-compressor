# Proposal: max-ratio-phase1-container

## Why

质量 codec 探针已经证明 fqzcomp 在 HiSeq、NovaSeq RNA-Seq 头部与 WGS 头部都稳定优于
zstd19，但探针不写归档，无法回答“收益能否落成可校验文件”。同时，`fqc-sequential/v2`
的帧语义、兼容性与 fast 路径不应被实验性质量 codec 污染。

本 change 实现一个独立的 Phase 1 实验容器 `fqc-max/v1`，把质量 codec 收益变成实际归档，
并暴露块头、校验和、空归档、截断输入等容器级问题。它是 max-ratio 路线的**质量/容器底座**，
不是最终最高压缩比方案；参考映射（Phase 2）与全局伪基因组（Phase 3）仍未实现。

> 追溯说明：设计文档要求“先格式契约、后实现”。本轮容器原型先于本提案落地，随后补此契约
> 文档，目的是冻结已实现格式并明确边界，不把原型状态伪装成正式格式。正式对外格式仍需
> 后续 ADR/格式评审。

## What Changes

- 新增 `fqc-max/v1` 归档：
  - 独立 magic `FQCMX1\r\n`、全局头、块头、footer；
  - ID 与 2-bit packed sequence 复用 v2 的逻辑流编码，但外层独立 zstd；
  - 质量流按块在 htscodecs fqzcomp（策略 0/1 自适应）与 zstd19 之间取较小者；
  - 块级与全局 XXH64 校验。
- 新增实验 CLI：`max-compress` / `max-decompress` / `max-verify`（别名 `mc`/`mx`/`mv`）。
- 复用现有 `FastqParser` 与 canonical FASTQ 输出；**不**声称保留 `+` 行内容、CRLF/LF
  混合行尾或空 read 的原始字节。
- vendor htscodecs `v1.6.7` 最小 C 子集为私有静态库 `fqc_fqzcomp`；不安装、不暴露 C API。
- 新增单元测试与 e2e round-trip；默认 CLI、`fqc-sequential/v2` 与 fast 流水线行为不变。

## Non-Goals

- 不实现 `--max-ratio` 顶层开关、参考基因组/BAM sidecar、伪基因组或 read 重排。
- 不冻结最终 max-ratio 的线上格式；`fqc-max/v1` 是实验容器，后续可能升版或废弃。
- 不把 fqzcomp 接入 `fqc-sequential/v2` 解码路径。
- 不承诺 paired-end 联合压缩、完整 35× WGS 或长读。
- 不设速度/内存门禁；`--zstd-level 19` 的压缩时间不是本轮优化目标。

## Impact

- 代码：`include/fqc/format/max_ratio.h`、`include/fqc/commands/max_ratio_engine.h`、
  `src/format/max_ratio.cpp`、`src/commands/max_ratio_engine.cpp`、`src/main.cpp`、
  `src/CMakeLists.txt`、`tests/format/max_archive_test.cpp`、`tests/e2e/cli_smoke_test.sh`。
- 构建：根 `CMakeLists.txt` 启用 C 语言与 `Threads`；Conan `exports_sources` 增加
  `third_party/*`。
- 依赖：vendored htscodecs `v1.6.7`（BSD-3-Clause），仅 `fqzcomp_qual.c` 与 `utils.c`。
- 默认行为：不变；现有 v2 归档仍可解压/校验。
