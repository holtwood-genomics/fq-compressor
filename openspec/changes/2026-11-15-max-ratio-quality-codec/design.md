# Design: max-ratio-quality-codec

## Goal

在不改变产品格式与 CLI 的前提下，验证 htscodecs fqzcomp 是否足以作为 max-ratio 的
质量流默认候选，并确定块大小、策略与回退条件。

## Measurement Model

### Inputs

- HiSeq 全质量：25,000 reads / 2,525,000 bases；见
  [datasets.md](../../../docs/max-ratio-baselines/datasets.md)。
- NovaSeq RNA-Seq 分箱质量：`DRR174812` R1 头部 21,110,784 B gzip，303,610 reads /
  45,541,500 bases。
- WGS 分箱质量：`SRR14724532` R1 头部 21,110,784 B gzip，298,974 reads /
  45,145,074 bases；完整 35× run 未下载。

### Codec boundary

- fqzcomp 输入为 quality ASCII 减 33 的无分隔二进制流；记录长度放入 `fqz_slice.len`，
  `flags=0`（不模拟 read2/reverse）。
- `fqz_compress(vers=4, strat 0..3, gp=NULL)`；解码后逐字节 `memcmp` 并核对每条长度。
- zstd 对照压缩同一 quality 二进制流，level 19。
- 当前 FQC 质量流对照取 `fqc_ratio_probe --zstd-level 1` 的 `quality_zstd`；它带记录
  换行，与 fqzcomp 输入不是完全同一字节流，报告中明确标注。

### Block model

- 按 read 条数切块仅用于实验；产品应把块目标定义为 **bases**，建议从 4M bases/块起步。
- 每块独立训练 fqzcomp 模型；小块会因 warmup 变差。
- 默认每块试策略 0/1，取较小者；策略 2/3 仅作为实验选项。

## Results

| 数据集 | quality 原始 | zstd19 | fqzcomp best | 质量流缩减 | 估算总归档缩减 |
|---|---:|---:|---:|---:|---:|
| HiSeq 25k | 2,525,000 | 678,308 | 558,189（strat 1） | 17.7% | 16.6%（估算） |
| NovaSeq 头部 | 45,541,500 | 2,802,326 | 2,159,788（strat 0） | 22.9% | 16.3%（估算） |
| WGS 头部 | 45,145,074 | 2,823,235 | 2,168,649（adaptive 0/1） | 23.2% | 15.5%（估算） |

分块曲线：

- HiSeq 1,000 reads/块 725,958 B，比整文件差 30.1%；10,000 reads/块仍差 5.3%；
- NovaSeq 25,000 reads/块 2,193,680 B，比整文件差 1.6%；自适应 0/1 为 2,171,444 B；
- 结论：4M bases/块是保守起点；更小块必须合并或拒绝。

## Container implications（TBD，不冻结字节）

质量块至少需要：

- codec id（fqzcomp / zstd fallback）；
- fqzcomp 策略 id；
- 未压缩 quality 字节数、记录数、每条长度；
- 压缩载荷长度；
- 块/全局校验和；
- 解码端的长度/值域/越界 fail-closed 检查。

这些字段在 Phase 1 格式契约中定义；本 change 不新增 magic 或生产格式。

## Risks

- NovaSeq 是 RNA-Seq 头部子集，WGS 是约 300k reads 头部子集；收益可能随文库/仪器/
  完整 run 漂移。
- fqzcomp 的 `int` 大小限制、参数块兼容性和解码失败路径需要格式契约保护。
- 外部 htscodecs 源码树构建不可 CI 化；生产集成需要单独的依赖/许可/构建决策。
- 估算未计入新容器、参数块、索引、ID/metadata 变化；不能当作已实现归档比。

## Rejected alternatives

- **直接集成产品**：格式契约未冻结，先把实验与生产隔离。
- **只测整文件**：会掩盖块大小/warmup，无法指导格式块目标。
- **固定 zstd 用于 NovaSeq**：本地数据不支持该假设；保留回退而不是预设赢家。
