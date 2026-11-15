# 2026-11-15 质量流 codec 基线：fqzcomp vs zstd

- 状态：Phase 1 质量 codec 探针（开发实验，非产品实现）
- 工具：htscodecs `v1.6.7` 的 `fqzcomp_qual` + zstd `1.5.7`
- 输入：HiSeq 全质量 25k reads；NovaSeq RNA-Seq 分箱质量头部 303,610 reads；
  WGS 分箱质量头部 298,974 reads
- 探针：[tools/quality_probe/README.md](../../tools/quality_probe/README.md)
- 数据集：[datasets.md](datasets.md)
- 设计：[docs/max-ratio-design.md](../max-ratio-design.md)

本实验只压缩 **quality 流**，不写归档、不改 `fqc-sequential/v2`。所有“归档影响”都是
“保持当前 sequence/ID/container 字节不变，仅把质量流替换为 fqzcomp”的估算，不是已实现
max-ratio 归档的实测结果。

## 1. 结论

1. **fqzcomp 在全质量与分箱质量上都稳定赢 zstd19。**
   - HiSeq 全质量：558,189 B vs 678,308 B，质量流小 **17.7%**；
   - NovaSeq RNA-Seq 分箱质量：2,159,788 B vs 2,802,326 B，质量流小 **22.9%**；
   - WGS 分箱质量头部：2,168,649 B vs 2,823,235 B，质量流小 **23.2%**。
2. **相对当前 FQC 的质量流（zstd level 1），收益更大。**
   - HiSeq：828,713 → 558,189 B，质量流小 **32.6%**；按当前归档估算，总归档小
     **16.6%**（1,634,437 → 1,363,913 B）。
   - NovaSeq RNA-Seq 头部子集：4,827,256 → 2,159,788 B，质量流小 **55.3%**；按当前
     归档估算，总归档小 **16.3%**（16,344,935 → 13,677,467 B）。
   - WGS 头部子集：4,779,205 → 2,168,649 B，质量流小 **54.6%**；按当前归档估算，
     总归档小 **15.5%**（16,811,413 → 14,200,857 B）。
3. **Phase 1 的“质量流稳定缩减 ≥10%”停止条件已通过。** 三个样本（全质量、RNA-Seq
   分箱、WGS 分箱）都远超 10%，且分块/分片重复实验没有反转。可以进入 Phase 1 容器格式
   与质量 codec 集成设计；但仍不能据此宣称 18–20× 总归档比，因为序列流尚未处理。
4. **设计假设“NovaSeq 分箱质量上 zstd 可能赢”在本地样本中不成立。** fqzcomp 的
   位置/前序质量上下文仍优于 zstd 的 LZ 匹配；RNA-Seq 与 WGS 头部都领先约 23%。
   保留 zstd 回退仍是防御性选择，但不应预设 fqzcomp 在分箱质量上会输。
5. **块大小必须是“百万 base 级”，不能按固定 read 条数切。** HiSeq 1,000 reads
   （约 101k bases）一块比整文件差 30.1%；2,000 reads（约 202k bases）仍差 21.0%。
   NovaSeq 分箱质量对块大小不敏感，但 25k reads（3.75M bases）一块仍比整文件差 1.6%。
   建议 Phase 1 以 **4M bases/块** 为起点，而不是 25k reads/块。

## 2. 固定版本与许可

| 项 | 值 |
|---|---|
| htscodecs tag | `v1.6.7` |
| tag object | `9f11619515c50186778c2820e8dbd322d78f24c2` |
| peeled commit | `b9fc194f772e45bb0a1f44b08cbf8697a1384bae` |
| tarball | `https://github.com/samtools/htscodecs/archive/refs/tags/v1.6.7.tar.gz` |
| tarball SHA-256 | `4f36d2800e97468435f724a4a0b26dfda6344b11dc16d3fa50b03f47b862e15a` |
| 质量模型版本 | `FQZ_VERS 5` |
| 许可证 | BSD-3-Clause；`c_range_coder.h`、`rANS_*` 为 Public Domain/CC0（逐文件头核对） |

本实验只编译 `fqzcomp_qual.c` 与 `utils.c`，使用空 `config.h`（仅定义
`HAVE_BUILTIN_PREFETCH`），没有把 htscodecs 加入当时的探针构建。后续 Phase 1 容器已
决定 vendor 同一版本的最小 C 子集（BSD-3-Clause），见
[`third_party/htscodecs/README.md`](../../third_party/htscodecs/README.md) 与
[格式契约](../../openspec/changes/2026-11-15-max-ratio-phase1-container/design.md)；
本质量探针仍保持独立构建，不进入产品 CMake。

## 3. 方法

- FASTQ 4 行结构解析，支持 LF/CRLF、空 read、literal `+` 行；quality 必须是
  Phred+33 可打印字节（33..126），否则 fail closed。
- fqzcomp 输入：每条 read 的 quality ASCII 减 33，拼接为无分隔二进制流；记录长度放入
  `fqz_slice.len`，`flags=0`（本实验没有模拟 read2/reverse）。
- `fqz_compress(vers=4, strat, gp=NULL)`；`strat` 取 0–3；解码后用 `memcmp` 比较
  quality 值并核对每条长度。
- zstd 基线：同一 quality 二进制流，`ZSTD_compress` level 19；当前 FQC 质量流对照取
  `fqc_ratio_probe --zstd-level 1` 的 `quality_zstd`（含记录 `\n`，与 fqzcomp 无分隔
  输入相差很小，但不是完全同一字节流）。
- 所有结果都是单线程、`-O2`、Debug/Release 混用不影响字节数的体积实验；时间只作方向性
  记录，不作速度门禁。

## 4. 整文件结果

### 4.1 HiSeq 全质量

输入：25,000 reads / 2,525,000 bases；quality ASCII 2,525,000 B。

| codec | 字节 | 相对 quality 原始 | bits/base | 相对 zstd19 |
|---|---:|---:|---:|---:|
| zstd level 1（当前 FQC 对照） | 828,713 | 3.05× | 2.626 | — |
| zstd level 19 | 678,308 | 3.72× | 2.149 | 1.00× |
| fqzcomp strat 0 | 560,431 | 4.51× | 1.776 | **−17.4%** |
| fqzcomp strat 1 | **558,189** | **4.52×** | **1.769** | **−17.7%** |
| fqzcomp strat 2 | 569,351 | 4.44× | 1.804 | −16.1% |
| fqzcomp strat 3 | 589,974 | 4.28× | 1.869 | −13.0% |

- 最佳：strat 1；strat 0 只差 2,242 B（0.4%）。
- fqzcomp 压缩输出重复运行的 SHA-256 一致：
  `652c522515d81313eeb3cf8de8b012c9759f5f7113544703f0dd5ad92d3b50de`
  （558,189 B）。
- 方向性时间：压缩约 0.095 s、解压约 0.07 s（单线程，非 benchmark）。

### 4.2 NovaSeq 分箱质量头部子集

输入：303,610 reads / 45,541,500 bases；quality 4 个符号，`F` 占 92.9%。

| codec | 字节 | 相对 quality 原始 | bits/base | 相对 zstd19 |
|---|---:|---:|---:|---:|
| zstd level 1（当前 FQC 对照） | 4,827,256 | 9.43× | 0.848 | — |
| zstd level 19 | 2,802,326 | 16.25× | 0.492 | 1.00× |
| fqzcomp strat 0 | **2,159,788** | **21.09×** | **0.379** | **−22.9%** |
| fqzcomp strat 1 | 2,171,156 | 20.98× | 0.381 | −22.5% |
| fqzcomp strat 2 | 2,179,970 | 20.89× | 0.383 | −22.2% |
| fqzcomp strat 3 | 2,242,782 | 20.31× | 0.394 | −20.0% |

- 最佳：strat 0；strat 1 只差 11,368 B（0.5%）。
- fqzcomp 压缩输出重复运行的 SHA-256 一致：
  `0f9fe88e5a9a1cf4a2d4c60b280236f845c46aff8b92e2013eb2355b167eeff1`
  （2,159,788 B）。
- 方向性时间：压缩约 0.65 s、解压约 0.51 s（单线程，非 benchmark）。

### 4.3 WGS 分箱质量头部子集

输入：298,974 reads / 45,145,074 bases；4 个质量符号，平均 Q35.68；WGS 头部子集。

| codec | 字节 | 相对 quality 原始 | bits/base | 相对 zstd1 |
|---|---:|---:|---:|---:|
| zstd level 1（当前 FQC 对照） | 4,779,205 | 9.45× | 0.847 | — |
| zstd level 19 | 2,823,235 | 15.99× | 0.500 | — |
| fqzcomp 自适应 0/1（单块） | **2,168,649** | **20.82×** | **0.384** | **−54.6%** |

- 相对 zstd19：2,168,649 vs 2,823,235，小 **23.2%**。
- 25k reads/块自适应 0/1：2,180,914 B（12 块），比单块差 0.57%；zstd19 同分块
  2,872,160 B，fqzcomp 仍小 24.1%。
- 当前 FQC 归档：16,811,413 B（SHA `e01c96…`）；只替换质量流估算
  14,200,857 B，总缩减 **15.5%**。
- 这是 WGS 头部约 300k reads，不是完整 35× run；序列跨 read 冗余不代表完整 run。

## 5. 分块曲线

### 5.1 HiSeq（fqzcomp strat 1）

| 块大小（reads） | 约 bases/块 | 块数 | 压缩字节 | 相对整文件 |
|---|---:|---:|---:|---:|
| 500 | 50.5k | 50 | 793,328 | +42.1% |
| 1,000 | 101k | 25 | 725,958 | +30.1% |
| 2,000 | 202k | 13 | 675,310 | +21.0% |
| 5,000 | 505k | 5 | 609,144 | +9.1% |
| 10,000 | 1.01M | 3 | 587,591 | +5.3% |
| 25,000 | 2.525M | 1 | 558,189 | 0% |

两个 1.2625M-base 半文件：285,538 + 289,737 = 575,275 B，比整文件差 3.1%。
说明 HiSeq 全质量模型需要百万 base 级样本才能 amortize 参数/模型 warmup。

### 5.2 NovaSeq（fqzcomp strat 0）

| 块大小（reads） | 约 bases/块 | 块数 | 压缩字节 | 相对整文件 |
|---|---:|---:|---:|---:|
| 1,000 | 150k | 304 | 2,214,651 | +2.5% |
| 2,000 | 300k | 152 | 2,358,193 | +9.2% |
| 5,000 | 750k | 61 | 2,275,264 | +5.3% |
| 10,000 | 1.5M | 31 | 2,232,151 | +3.3% |
| 25,000 | 3.75M | 13 | 2,193,680 | +1.6% |
| 50,000 | 7.5M | 7 | 2,190,788 | +1.4% |
| 100,000 | 15M | 4 | 2,174,463 | +0.7% |
| 303,610 | 45.54M | 1 | 2,159,788 | 0% |

自适应 strat 0/1、25k reads/块：2,171,444 B（13 块），比固定 strat 0 的 2,193,680 B
好 1.0%，比整文件单块差 0.54%。

四个约 11.385M-base 分片：
536,853 + 535,783 + 566,670 + 539,462 = 2,178,768 B，比整文件差 0.88%。
同一分片的 zstd19 总和 2,835,385 B；fqzcomp 仍小 23.2%。

## 6. 对归档的影响估算

当前 FQC 归档使用 `clang-debug`、`--frame-mib 64`、`--parse-workers 4`、
`--quality-level 1`。质量流替换估算如下：

| 数据集 | 当前归档 | 当前 quality zstd1 | fqzcomp best | 估算新归档 | 估算总缩减 | 估算 raw/归档 |
|---|---:|---:|---:|---:|---:|---:|
| HiSeq 25k | 1,634,437 | 828,713 | 558,189 | 1,363,913 | **16.6%** | 4.794× |
| NovaSeq 头部 | 16,344,935 | 4,827,256 | 2,159,788 | 13,677,467 | **16.3%** | 7.331× |
| WGS 头部 | 16,811,413 | 4,779,205 | 2,168,649 | 14,200,857 | **15.5%** | 7.037× |

口径：

- 当前归档 SHA：HiSeq `19bdd931f2332376209c512d6c18228058949d8239131e8b626c9be528236b1c`；
  NovaSeq 子集 `9d17706bf777ad9b7a66c80ad843d862e8a187aefe24746987234b8f2ec8221a`；
  WGS 头部 `e01c96e6b34f63822d0857cc8ab7ff4a83c35ac8ee1bfa7d91dca511ebc3ee6e`。
- `当前 quality zstd1` 来自 `fqc_ratio_probe --zstd-level 1` 的 `quality_zstd`，是带
  记录换行的流级基线；fqzcomp 输入不带换行。两者相差很小，但不是逐字节同一输入。
- 估算没有计入 max-ratio 新容器头、块索引、fqzcomp 参数块、校验和或 ID/metadata 变化；
  实际 Phase 1 归档必须重新测量。不能把 16.6%/16.3% 当作已实现数字。
- 估算也没有改变 sequence 流；NovaSeq 剩余归档仍以 2-bit 序列为主，距离 18–20× 原始
  还差全局序列冗余（Phase 2 参考路线 / Phase 3 无参路线）。

## 7. 设计结论

### 7.1 质量 codec

- **默认候选：fqzcomp，strat 0 与 1 每块试编码，取较小者。** strat 2/3 在本轮两个
  数据集上都更差，不进入默认候选；WGS 头部也采用自适应 0/1。
- 块目标从 **4M bases** 起步；按 bases 而不是 reads 切块，避免 read 长度变化导致模型
  warmup 成本漂移。小 read 数/小 base 块必须被格式契约拒绝或合并。
- 保留 zstd 回退，但回退条件是“本块 fqzcomp 产物更大/编码失败”，不是“NovaSeq 一定
  用 zstd”。本实验没有观察到分箱质量上 zstd 赢。
- fqzcomp 参数/模型头、块长度表和策略 id 必须进入格式契约；不能只存裸压缩字节。
- 解码端必须校验每条长度、总 base 数和 quality 值域；fqzcomp 失败必须 fail closed。

### 7.2 下一步

Phase 1 的顺序应是：

1. 写 `openspec/changes/` 的质量块格式契约（块大小、codec id、策略 id、长度表、
   checksum、zstd 回退、畸形输入拒绝）；
2. 在独立 max-ratio 容器原型中集成 fqzcomp；序列暂时保持 2-bit + zstd；
3. 用 HiSeq + NovaSeq 头部 + WGS 头部子集做逐字节 round-trip 和归档实测；
4. 只有质量/ID/metadata 层稳定后，才进入 Phase 2 `max-ref` 序列参考编码。

**2026-11-15 更新**：上述 1–3 已由 `fqc-max/v1` 完成，4 已由 Phase 2 开发工具
`fqc_ref_archive` 的 PAF 参考原型开始验证；本文件仍只记录质量流探针，不把容器/参考结果
混入质量 codec 数字。Phase 1 容器见
[2026-11-15-max-container.md](2026-11-15-max-container.md)，Phase 2 参考原型见
[2026-11-15-ref-container.md](2026-11-15-ref-container.md)。

## 8. 未验证边界

- NovaSeq 是 RNA-Seq 头部子集，不是完整 run；分箱质量行为可参考，但不能代表所有
  NovaSeq 文库。
- WGS `SRR14724532` 已下载头部约 300k reads；完整 35× run 未下载，不能代表完整 run
  的覆盖分布/序列冗余。
- 没有模拟 paired-end read2/reverse flags；R2 质量分布可能不同。
- 没有测试混合质量字母表、非法 quality 字节、空 read 在 fqzcomp 中的完整边界；
  探针对这些输入 fail closed，但产品格式需要独立 fixture。
- 没有测 fqzcomp 的压缩/解压吞吐、内存峰值或并行扩展；Phase 1 不设速度门禁。
- 没有评估 htscodecs name tokeniser、rANS 静态模型或 ID delta；ID 仍按当前 zstd 基线
  计入估算。
- 没有复测 ORA/Genozip/SPRING/PgRC2；外部数字仍是调研证据，不是本地结论。

## 9. 复现命令

```bash
# 1. 固定 htscodecs
curl -L -o /tmp/htscodecs-v1.6.7.tar.gz \
  https://github.com/samtools/htscodecs/archive/refs/tags/v1.6.7.tar.gz
sha256sum /tmp/htscodecs-v1.6.7.tar.gz
mkdir -p /tmp/htscodecs-v1.6.7
tar -xzf /tmp/htscodecs-v1.6.7.tar.gz \
  -C /tmp/htscodecs-v1.6.7 --strip-components=1

# 2. 构建探针（从 Conan 缓存取 zstd/1.5.7）
./tools/quality_probe/build.sh /tmp/htscodecs-v1.6.7

# 3. HiSeq
build/quality-probe/fqc_quality_probe /tmp/fqc-exp/hiseq_R1.fastq \
  --block-records 0 --json
build/quality-probe/fqc_quality_probe /tmp/fqc-exp/hiseq_R1.fastq \
  --block-records 10000 --json

# 4. NovaSeq 头部子集
build/quality-probe/fqc_quality_probe \
  /tmp/fqc-exp/novaseq/DRR174812_R1.head.fastq \
  --block-records 25000 --json

# 5. WGS 头部子集
build/quality-probe/fqc_quality_probe \
  /tmp/fqc-exp/wgs/SRR14724532_R1.head.fastq \
  --block-records 0 --json

# 6. 当前 FQC 归档基线（仅对照，不是 max-ratio）
build/clang-debug/src/fqc compress \
  -i /tmp/fqc-exp/novaseq/DRR174812_R1.head.fastq \
  -o /tmp/fqc-exp/novaseq/current.fqc \
  --frame-mib 64 --parse-workers 4 --quality-level 1 --force
```

输入 SHA、完整 JSON 和快照范围见 [datasets.md](datasets.md) 与本文件各表；探针输出
JSON 的 `schema` 为 `fqc-quality-probe/v1`。
