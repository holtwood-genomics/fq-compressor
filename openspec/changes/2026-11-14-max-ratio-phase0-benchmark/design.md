# Design: max-ratio-phase0-benchmark

## Goal

在不改变产品格式与 CLI 的前提下，建立一个可重复、可审计的压缩比测量底座，回答：

1. 当前 FQC 在固定数据集上的 raw/gzip/归档比是多少；
2. 归档体积中 sequence、quality、ID、metadata 各占多少；
3. 与 `gzip -6`、`zstd -19` 及外部工具相比，差距落在哪条流；
4. 后续 `max-ref` / `max-ref-free` 的收益是否真实，而不是合成数据或口径误差。

Phase 0 不设吞吐门禁。速度数字只记录环境和 wall time，不据此否决压缩比方案。

## Measurement Model

### Inputs

- 普通 FASTQ 与 `.fastq.gz`；
- 首批使用 `docs/max-ratio-design.md#11-压缩比验收协议` 中列出的数据集；
- 每个数据集必须有 SHA-256、来源 URL/accession、下载日期、read 数、read 长度、质量分布摘要。

### Parsing boundary

当前 `fqc::io::FastqParser` 的已文档化行为：

- 保留 `id/comment/sequence/quality`，但**丢弃 `+` 行内容**；
- 对 CRLF 输入剥离行尾 `\r`；
- 拒绝空 sequence。

因此 Phase 0 分两层计量：

1. **兼容层**：使用现有 parser 快速得到 record/bases/质量统计，明确标注其无损边界；
2. **原始字节层**：为 max-ratio 预研一个只读扫描器，保留 `+` 行、CRLF/LF、空 read 与异常
   字节，输出按流字节数；该扫描器在 Phase 0 只用于测量，不进入产品路径。

两层结果不得混在同一列。若原始字节层尚未实现，报告必须写“当前 parser 不支持该边界”，
不能用兼容层结果宣称逐字节无损。

### Stream accounting

对每条记录累计：

| 流 | 计入内容 |
|---|---|
| header | `@`、ID、comment、行尾 |
| sequence | 序列字节 + 行尾 |
| plus | `+` 行全部字节 + 行尾 |
| quality | 质量字节 + 行尾 |
| inter-record | 记录间空行/异常空白（若存在） |

同时输出“逻辑字段字节”和“含行尾原始字节”两套口径；后者用于最终 `cmp` 验收。

### Baselines

- `gzip -6`：与业界最常用比较口径对齐；
- `zstd -19`：通用高压缩比参考，不代表 FASTQ 专用模型；
- 当前 FQC：固定 commit、profile、帧大小、parse workers、quality level；
- 外部工具：只在可获得、可复现时运行；否则引用论文/官方数字并标注证据等级。

所有命令、版本、线程数、内存上限、重复次数写入结果 manifest。

## Probe Contract

建议开发目标名：`fqc_ratio_probe`（不安装、不进入发布包）。输入输出：

```text
fqc_ratio_probe --input <fastq|fastq.gz> [--json] [--zstd-level 19]
```

输出至少包含：

```json
{
  "input": {"path": "...", "sha256": "...", "bytes": 0},
  "records": 0,
  "bases": 0,
  "n_bases": 0,
  "quality": {"symbols": 0, "mean_q": 0.0},
  "raw_streams": {"header": 0, "sequence": 0, "plus": 0, "quality": 0, "line_endings": 0},
  "packed_sequence_bytes": 0,
  "baselines": {"gzip6": 0, "zstd19": 0},
  "ratios": {"raw_over_gzip": 0.0, "raw_over_fqc": 0.0, "fqc_over_gzip": 0.0},
  "warnings": []
}
```

要求：

- 同一输入重复运行，计数与字节数完全一致；
- 畸形输入 fail closed，返回非零并指出行号/字段；
- 不写归档、不改输入、不依赖未提交的 `/tmp` 文件；
- gzip 输入通过现有 `io::openInputFile` 读取，报告解压后逻辑字节与压缩文件字节两套口径。

## Dataset Manifest

首批建议：

1. 本地 HiSeq 小样本：`hiseq_R1.fastq`，SHA-256 已记录于设计文档 §4.1；
2. NovaSeq 分箱质量短读：优先复用 fqxv benchmark 的公开数据，记录 accession/下载日期；
3. 深覆盖人类 WGS 短读：GIAB/ENA `SRR14724532`，头部子集与完整 run manifest 均记录
   accession、覆盖度、read 数与 SHA；完整 35× run 未下载；
4. 长读仅作为后续扩展，不进入首轮短读基线表。

任何数据集若无法固定来源或 SHA，只能标为“探索性”，不得进入验收表。

## Acceptance Criteria

- 同一数据集重复运行，record/bases/stream bytes 完全一致；
- 报告能区分 raw、gzip、归档、bits/base 四种口径；
- 当前 FQC 的本地 HiSeq 结果与设计文档 §4.1 的 4.0006× raw / 1.197× gz 对齐（允许因
  commit/参数不同而标注差异）；
- `+` 行、CRLF、空 read 的边界在报告中显式标注“已支持/未支持”，不得静默略过；
- 不产生新格式、不改变默认 CLI。

## Risks

- **parser 边界不足**：现有 parser 无法测 `+` 行/CRLF/空 read；缓解：原始字节层单独实现，
  或在报告中明确限制。
- **数据集漂移**：公开 URL 内容可能变化；缓解：SHA-256 + 下载日期 + 本地快照。
- **外部工具不可得**：SPRING/PgRC2/fqzcomp 可能无法在当前环境构建；缓解：引用来源并标注
  未本地复测，不混入本地 baseline。
- **口径污染**：把 DNA-only bpb、有损质量或参考不计入的体积当完整 FASTQ 比；缓解：证据
  等级/口径标签，报告分列。
