# 2026-11-15 实验容器 `fqc-max/v1` 实测

- 状态：Phase 1 实验容器（ratio-first 原型，**不是**最终 max-ratio，不是默认格式）
- 实现：`max-compress` / `max-decompress` / `max-verify`（`src/format/max_ratio.cpp`、
  `src/commands/max_ratio_engine.cpp`）
- 质量 codec：htscodecs `v1.6.7` fqzcomp（每块策略 0/1 自适应，zstd19 回退）
- ID/序列：复用 v2 的 ID 流与 2-bit packed sequence 逻辑流，外层 zstd19
- 输入/数据集：[datasets.md](datasets.md)
- 设计：[docs/max-ratio-design.md](../max-ratio-design.md)
- 质量流探针：[2026-11-15-quality-codecs.md](2026-11-15-quality-codecs.md)
- 格式契约：[openspec/changes/2026-11-15-max-ratio-phase1-container/design.md](../../openspec/changes/2026-11-15-max-ratio-phase1-container/design.md)

本文件记录**实际写出的 `fqc-max/v1` 归档**，不是“只替换质量流”的估算。所有数字都在
2026-11-15 的 `clang-debug` 构建上重复生成，输入与输出 SHA-256 均已固定。

## 1. 结论

| 数据集 | 原始 FASTQ | gzip | 当前 v2 归档 | `fqc-max/v1` | 相对 v2 | raw / max | gzip / max |
|---|---:|---:|---:|---:|---:|---:|---:|
| HiSeq 25k R1 | 6,538,676 | 1,957,197 | 1,634,437 | **1,285,285** | **−21.36%** | **5.087×** | **1.523×** |
| NovaSeq RNA-Seq 头部 | 100,272,700 | 21,110,784 | 16,344,935 | **12,280,644** | **−24.87%** | **8.165×** | **1.719×** |
| WGS 头部 | 99,934,080 | 21,110,784 | 16,811,413 | **12,828,169** | **−23.69%** | **7.790×** | **1.646×** |

关键结论：

1. **质量 codec 的收益能落成实际归档。** 三个样本都通过 `max-verify` 与解压后 `cmp`，
   不是估算。
2. **实际缩减大于质量流估算（15.5%–16.6%）。** 原因是 `fqc-max/v1` 对 ID 与 packed
   sequence 也使用 zstd19，而当前 v2 基线使用较低档位；因此不能把 −21%～−25% 全部归因于
   fqzcomp。质量流的独立贡献仍以
   [2026-11-15-quality-codecs.md](2026-11-15-quality-codecs.md) 为准。
3. **仍然远未达到 20×+。** HiSeq 5.09× raw 只比 v2 的 4.00× 高约 27%；NovaSeq/WGS 头部
   约 7.8×–8.2× raw。要进入 ORA/Genozip/SPRING 的第一梯队，仍必须做 Phase 2 参考映射的
   真实 WGS 验收或 Phase 3 全局伪基因组/差异编码，而不是继续调容器。Phase 2 的 PAF 参考
   原型见 [2026-11-15-ref-container.md](2026-11-15-ref-container.md)。
4. **这是 Phase 1 原型，不是最终字节保真边界。** 输入走生产 `FastqParser`，输出是 canonical
   LF FASTQ：`+` 行内容、CRLF/LF 混合行尾、空 read 的原始字节不在本容器保证范围内。最终
   max-ratio 必须满足 [设计文档 §2.3](../max-ratio-design.md#23-无损边界) 的逐字节边界。

## 2. 固定输入与输出

### 2.1 HiSeq 25k R1

| 项 | 值 |
|---|---|
| 输入 | `/tmp/fqc-exp/hiseq_R1.fastq`，6,538,676 B |
| 输入 SHA-256 | `de63a088f1a748153e0a42814dbb4e7dd8fb95a01fb2b2bafd51a2e975075058` |
| 当前 v2 | 1,634,437 B，SHA-256 `19bdd931f2332376209c512d6c18228058949d8239131e8b626c9be528236b1c` |
| `fqc-max/v1` | 1,285,285 B，SHA-256 `1c9f538483892ae2b507347454f15c01f50472417d2c3ff7225f3336150a8dc5` |

### 2.2 NovaSeq RNA-Seq 头部

| 项 | 值 |
|---|---|
| 输入 | `/tmp/fqc-exp/novaseq/DRR174812_R1.head.fastq`，100,272,700 B |
| 输入 SHA-256 | `c8c02eccca86ee2a67f7ef84a0d36706f25ec9426dc7b1e531b5686bafb5b40e` |
| 当前 v2 | 16,344,935 B，SHA-256 `9d17706bf777ad9b7a66c80ad843d862e8a187aefe24746987234b8f2ec8221a` |
| `fqc-max/v1` | 12,280,644 B，SHA-256 `be8239481fb62b760292c9349ba949d40b13f9dbe8c1c218e19b7683f76ce47f` |

### 2.3 WGS 头部

| 项 | 值 |
|---|---|
| 输入 | `/tmp/fqc-exp/wgs/SRR14724532_R1.head.fastq`，99,934,080 B |
| 输入 SHA-256 | `d4f6c8bee191ee8cc5207cca5fb86659a2aaa02e3f493b35f2ed50027ab6e0f4` |
| 当前 v2 | 16,811,413 B，SHA-256 `e01c96e6b34f63822d0857cc8ab7ff4a83c35ac8ee1bfa7d91dca511ebc3ee6e` |
| `fqc-max/v1` | 12,828,169 B，SHA-256 `4034b9d62fc8c249c91df7492a297430ee875044548294c50b91676ba09573c2` |

## 3. 复现命令

```bash
# 构建
./scripts/build.sh clang-debug

# HiSeq
./build/clang-debug/src/fqc -q max-compress \
  -i /tmp/fqc-exp/hiseq_R1.fastq \
  -o /tmp/fqc-exp/hiseq.fqmx --profile illumina
./build/clang-debug/src/fqc -q max-verify /tmp/fqc-exp/hiseq.fqmx
./build/clang-debug/src/fqc -q max-decompress \
  -i /tmp/fqc-exp/hiseq.fqmx -o /tmp/fqc-exp/hiseq.max.fastq
cmp -s /tmp/fqc-exp/hiseq_R1.fastq /tmp/fqc-exp/hiseq.max.fastq

# NovaSeq / WGS 同理，替换输入与输出路径
```

默认参数：`--block-bases 4000000`、`--max-block-mib 128`、`--zstd-level 19`、
`--profile illumina`。三个样本重复运行 `max-compress` 的输出 SHA 均与上表一致
（HiSeq `1c9f…`、NovaSeq `be82…`、WGS `4034…`），解压后 `cmp` 也独立通过。

## 4. 验证证据

| 检查 | 结果 |
|---|---|
| `./scripts/test.sh clang-debug` | 19/19 tests passed（当时；加入 Phase 2 的 `ref_archive_test` 后当前全量为 20/20） |
| `max-verify`（三个归档） | 全部通过；块数/记录数/总 bases/全局校验和一致 |
| `max-decompress` + `cmp`（三个归档） | 全部逐字节一致（canonical LF FASTQ） |
| `./scripts/lint.sh format-check` | 通过（clang-format-18） |
| `./scripts/notes.sh verify` | 通过 |
| `git diff --check` | 通过 |

`fqc-max/v1` 自带块级 XXH64 与全局滚动校验和；`max-verify` 会完整解码每个块，而不是只读
头部。截断归档、损坏块、未知 magic 由 `tests/format/max_archive_test.cpp` 覆盖。

## 5. 口径与未验证边界

- **相对 v2 的缩减不是纯质量 codec 收益。** `fqc-max/v1` 的 ID/sequence 外层是 zstd19，
  当前 v2 基线不是；质量流对照见
  [质量 codec 基线](2026-11-15-quality-codecs.md#6-对归档的影响估算)。
- **NovaSeq 是 RNA-Seq 头部子集；WGS 是约 300k reads 头部子集。** 不能代表完整 run 的
  跨 read 冗余或覆盖分布；完整 35× WGS 未下载。
- **canonical LF 边界。** `FastqParser` 会规范化 CRLF、丢弃 `+` 行内容，并拒绝空
  sequence；因此本容器不能宣称对任意 FASTQ 逐字节无损。最终 max-ratio 的 `+` 行/行尾/
  空 read 支持仍是 Phase 1 格式契约的未完成项。
- **没有 paired-end 联合压缩。** 当前容器每次只处理单端 R1；R2 需独立归档。
- **没有速度/内存门禁。** 本轮只验证压缩比与 round-trip；`--zstd-level 19` 的压缩时间
  明显高于 fast 路径，Phase 4 才优化。
- **没有实现 `--max-ratio` 顶层开关。** `max-compress` 是显式实验子命令；参考模式是另一个
  开发工具 `fqc_ref_archive`（Phase 2，见
  [2026-11-15-ref-container.md](2026-11-15-ref-container.md)），不在主 CLI。默认 CLI、
  `fqc-sequential/v2` 与 fast 路径行为不变。
