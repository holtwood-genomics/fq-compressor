# Agent Note: Phase 0 压缩比探针

Status: implemented

## Problem

max-ratio 设计需要一个可重复的压缩比底座，但仓库此前只有临时 Python 实验：

- 临时脚本未提交，不能作为正式 baseline；
- 现有 `FastqParser` 丢弃 `+` 行、规范化 CRLF、拒绝空 read，不能支撑逐字节无损测量；
- 外部论文/厂商数字混用 raw、`fastq.gz`、bits/base 和有损质量，不能直接当验收。

如果先写 max-ratio 格式或编码器，就没有可信的“改前/改后”字节账本。

## Decision

新增 `tools/ratio_probe`：

- 只读 scanner，独立于生产 `FastqParser`，保留 `+` 行、CRLF/LF、空 read 和无最终换行；
- 按 `header/sequence/plus/quality/blank` 分流统计 content、line-ending 与 total；
- 统计 records、bases、ACGT/N/other、质量符号/均值/非法字节、2-bit packed 大小和异常表；
- 用流式 zstd 和进程内 zlib 计算整文件与 ID/quality 流基线，不保存压缩产物；
- 输出 `fqc-ratio-probe/v1` JSON 或文本；`fqc_ratio_probe` 不安装、不进入产品 CLI；
- `ratio_probe_test` 覆盖 CRLF、空 read、`+` 行、无最终换行、畸形输入、gzip 文件和
  基线确定性。

已实现的文件：

- `tools/ratio_probe/ratio_probe.h`
- `tools/ratio_probe/ratio_probe.cpp`
- `tools/ratio_probe/main.cpp`
- `tools/ratio_probe/README.md`
- `tests/tools/ratio_probe_test.cpp`

## Alternatives considered

- **直接扩展 `FastqParser`**：会把生产 parser 的现有字段模型和 max-ratio 的字节保真需求
  耦合在一起；先保持独立 scanner，等格式契约定稿再决定共享解析层。
- **继续使用 `/tmp` Python 脚本**：开发快，但不可追溯、不可 CI 化，也无法保证与 C++
  读取/zlib/zstd 版本一致；因此只保留为历史方向性证据。
- **把 probe 做成 `fqc ratio-probe` 子命令**：会扩大产品 CLI 和用户支持面，Phase 0 还
  不需要；先做独立开发目标，后续若稳定再评估。
- **用外部 `gzip` 命令做基线**：可复现性受系统 gzip 版本/头部元数据影响；改用进程内
  zlib，并在 JSON 中明确 implementation。

## Consequences

- 收益：现在有可 CI 的流级测量工具；同一输入重复运行字节数稳定；能看到序列/质量/ID
  的真实占比，而不是只看总归档比。
- 代价：探针的 gzip 基线不是外部 `gzip -6` 的逐字节复刻；`id_zstd`/`quality_zstd` 使用
  归一化 `\n` 流，不代表最终容器布局。
- 边界：本笔记只覆盖 Phase 0 测量工具；`max-ref`/`max-ref-free`、新 magic、新格式和
  生产 parser 改写仍未实现。
- 验证：`clang-debug` 全量 18/18 测试通过；`ratio_probe_test` 10 项通过；格式检查通过。
