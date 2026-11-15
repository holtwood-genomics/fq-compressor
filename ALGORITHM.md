# 压缩算法与原理

本文解释 FQC v2 的压缩算法设计：为什么这样做、每一步在压什么、各步骤之间如何配合。
字节级布局见 [ARCHITECTURE.md](ARCHITECTURE.md)。

## 总体思路

FASTQ 每条记录有四个字段：标识符（ID + 注释）、序列、`+` 分隔行、质量值。
其中 `+` 行不携带信息，直接丢弃；其余三个字段按**列式分离**编码为三条独立的字节流，
各自用 Zstd level 1 压缩后写入帧。

```text
FASTQ 记录流
  ├─ ID 流:      varint(记录数) + [varint(id长度) + id + varint(注释长度) + 注释] × N
  ├─ 序列流:     varint(记录数) + [2-bit 打包 + 异常列表] × N
  └─ 质量值流:   varint(记录数) + [varint(长度) + 原始字节] × N
       ↓ 各自独立 Zstd level 1
  帧载荷 = 压缩ID + 压缩序列 + 压缩质量值
```

### 为什么列式分离

FASTQ 是行式格式——四个字段交替出现。但同一字段在不同记录之间有更强的统计相关性：

- **ID** 共享仪器名、泳道号、坐标前缀（如 `@M00123:45:000000000-ABCDE:1:1101:12345:1234`），
  前缀重复率极高。
- **序列** 的碱基分布和 k-mer 频率在同类数据中高度一致。
- **质量值** 的分布由测序化学决定，同一 run 内模式相似。

把三个字段分开，Zstd 的滑动窗口里全是同类数据，字典匹配效率远高于行式交错。
这是压缩比的主要来源之一。

### 为什么选 Zstd level 1

Level 1 在压缩比和吞吐量之间取了一个偏吞吐的平衡点：

- 吞吐实测见 [README](README.md#性能) 与 `docs/benchmarks.md`（数字随平台与阶段漂移）。
- 相比 level 3–5，level 1 的压缩比损失通常在 2–5%，但速度快 2–3 倍。
- 对于 FASTQ 这种列式分离后冗余已经大幅降低的数据，更高级别的边际收益递减。

Zstd 还自带帧校验（可选）和极快的解压路径，适合管道场景。

阶段 I 的准入门槛实测（详见 `docs/fastq-compression-survey.md` §6）进一步支持这一选择：
质量流单独提档，level 7/9 在拟真质量分布上仅省 3–4% 体积却付出 43–53% 压缩吞吐，
在 11 符号随机质量上 level 3–9 的归档反而更大。因此默认保持 level 1；
`compress --quality-level N`（1–19，只影响质量流）作为实验入口保留。

## 序列编码：2-bit 打包

这是压缩比贡献最大的单步操作。

### 编码规则

DNA 序列中 uppercase A/C/G/T 占绝大多数（Illumina 数据通常 >99%）。
四个碱基各用 2 bit 表示，4 个碱基打包进 1 字节，LSB 优先：

```text
碱基 → 2-bit 值:  A=0  C=1  G=2  T=3

序列 "ACGTACGT" 的打包结果:
  字节 0: A(00) C(01) G(10) T(11) → 0b11_10_01_00 = 0xE4
  字节 1: A(00) C(01) G(10) T(11) → 0xE4

打包后大小 = ⌈序列长度 / 4⌉ 字节
```

原始 FASTQ 中每个碱基占 1 字节（8 bit），打包后占 2 bit，**序列部分直接 4:1 压缩**，
在 Zstd 之前就已经完成。

### 异常碱基处理

不是所有碱基都是 uppercase A/C/G/T。IUPAC 字母表还包括：

- 小写碱基 `a, c, g, t`（软掩码，大小写有语义，必须保留）
- 简并碱基 `R, Y, S, W, K, M, B, D, H, V, N`

这些碱基无法用 2 bit 表示，按**异常列表**编码：

```text
varint(异常数量)
对每个异常:
  varint(位置增量)   ← 第一个是绝对位置，后续是相对前一个异常的间距
  1 字节原始 ASCII   ← 保留原始字符，大小写不丢失
```

打包时，异常位置的 2-bit 槽填 0（即 `A`），解码时先展开 2-bit 序列，
再用异常列表覆写对应位置。

**设计要点：**

- 对 Illumina 短读长，异常列表通常为空（只有 varint(0) 一个字节），开销几乎为零。
- 对长读长（ONT/PacBio），小写和简并碱基可能较多，增量编码避免为每个异常分配独立对象。
  旧版实现中临时 vector 可能比输入数据本身还大，v2 用一趟打包 + 一趟定位遍历解决。
- 解码侧会重新校验每个异常字节是否为合法 IUPAC 字符，拒绝非法输入。

### 压缩效果估算

以 150 bp Illumina 读长为例（全部 ACGT，无异常）：

| 阶段 | 每条记录序列大小 | 说明 |
|---|---:|---|
| 原始 FASTQ | 150 B | 1 字节/碱基 |
| 2-bit 打包后 | 38 B | ⌈150/4⌉ = 38 |
| Zstd 后 | ~25–30 B | 随机序列的 Zstd 增益有限 |

序列流的 4:1 打包是压缩比的最大单一贡献者。Zstd 在随机序列上增益不大（随机碱基的熵接近
2 bit/碱基，已接近理论极限），但在真实生物数据中，低复杂度区域和重复序列会带来额外收益。

## 质量值编码

质量值采用**最简策略**：原始 Phred+33 ASCII 字节直接拼接，交给 Zstd 压缩。

```text
varint(记录数)
对每条记录:
  varint(质量值长度) + 原始 ASCII 字节
```

没有做 delta 编码、游程编码、分箱（binning）或有损量化。原因：

1. **无损是硬约束。** 质量值承载变异检测的统计权重，任何有损变换都会改变下游分析结果。
2. **Zstd 已经够用。** 质量值的字母表只有 ~94 个可打印 ASCII 字符（`!` 到 `~`），
   且同一 run 内分布模式重复，Zstd 的字典匹配能有效利用这些冗余。
3. **专用编解码器尚未通过 fast 路径的准入门槛。** 架构文档中提到，所有 profile 目前共用同一套回退编码——
   还没有哪个专门的质量值编解码器能在足够大的真实语料上同时通过体积和吞吐量测试。

> 以上取舍限定于 **fast 流式路径**。最高压缩比专项重新评估质量值上下文建模与读取重排；
> 本地探针已验证 htscodecs fqzcomp 在 HiSeq 全质量比 zstd19 小 17.7%、在 NovaSeq
> RNA-Seq 分箱质量头部小 22.9%、在 WGS 分箱质量头部小 23.2%，而不是继续调 zstd 档位。设计见
> [docs/max-ratio-design.md](docs/max-ratio-design.md)，实验见
> [docs/max-ratio-baselines/2026-11-15-quality-codecs.md](docs/max-ratio-baselines/2026-11-15-quality-codecs.md)。
> 当前 CLI 有实验 `max-compress` 子命令与 `fqc-max/v1` 容器；参考路线另有开发工具
> `fqc_ref_archive` / `fqc-max-ref/v1`。`--max-ratio` 顶层开关、内置 mapper/BAM、Phase 3
> 伪基因组与最终逐字节格式仍未实现，Phase 2 真实 30× WGS 尚未验收。

**无损边界**：这里的"无损"指**字段级**无损——ID/sequence/quality 的字节序列在
压缩-解压往返后逐字节保留。文件级的逐字节往返（`cmp` 一致）还要求输入为 LF-only
规范 FASTQ：CRLF 输入的行尾 `\r` 被静默规范化，字段内容恰以单个 `\r` 结尾的 LF-only
文件同理（该字节与 CRLF 行尾在字节流上不可区分）。结构畸形的记录（缺 `+` 行、
sequence/quality 长度不一致）与超长单行会被拒绝，不产出归档。

## ID 流编码

```text
varint(记录数)
对每条记录:
  varint(id 长度) + id 字节 + varint(注释长度) + 注释字节
```

ID 和注释分开存储（FASTQ 头行以第一个空格分割），因为：

- 解压时需要精确重建 `@{id}[ {comment}]` 格式，包括有无注释、注释前的空格。
- ID 部分的重复前缀（仪器名、泳道号）是 Zstd 字典匹配的主要收益来源。

## 校验和设计

三层 XXH64 校验，覆盖从头部到帧尾的完整链路：

| 层级 | 覆盖范围 | 检测目标 |
|---|---|---|
| 全局头校验和 | 头部前 24 字节 | 版本/编解码器 ID 篡改、传输损坏 |
| 帧逻辑校验和 | 三路**未压缩**流的链式哈希 | 载荷数据损坏（即使 Zstd 解压侥幸成功也能发现） |
| Footer 滚动校验和 | 逐帧校验和的累积哈希 | 帧丢失、重复、重排序 |

**关键设计：帧校验和计算在未压缩流上，而非压缩流上。**
这意味着即使 Zstd 解压因损坏数据产生了错误输出（而非报错），校验和不匹配仍会触发 fail-closed。

链式计算顺序：`XXH64(质量值流, XXH64(序列流, XXH64(ID流, 0)))`。
Footer 的滚动校验和把每帧的 8 字节校验和依次喂入全局 XXH64，
因此任何帧级别的增删改都会导致最终校验和不匹配。

## Varint 编码

所有长度、计数、位置增量均使用 LEB128 风格的无符号 varint：

- 每字节 7 bit 有效载荷，最高位为续传标志。
- 最多 10 字节（64 bit），解码时检查 shift == 63 溢出。
- 小值（<128）只占 1 字节，对短读长的长度字段和空异常列表非常友好。

## 端到端压缩比分析

以 150 bp Illumina 随机合成数据为例（实测压缩比 ~2.95×）：

| 组件 | 原始大小/记录 | 压缩后估算 | 主要压缩手段 |
|---|---:|---:|---|
| 序列 (150 bp) | 150 B | ~25–30 B | 2-bit 4:1 打包 + Zstd |
| 质量值 (150 chars) | 150 B | ~80–100 B | Zstd 字典匹配 |
| ID + 注释 (~60 chars) | 60 B | ~10–15 B | Zstd 前缀匹配 |
| FASTQ 框架 (`@`, `+`, 换行) | 6 B | 0 B | 丢弃，解压时重建 |
| **合计** | **~366 B** | **~120 B** | **~3×** |

质量值是压缩比的主要瓶颈——它占原始数据约 40%，但 Zstd 压缩后仍占较大比例。
真实短读（质量符号少、分布集中）通常优于随机合成；质量字母表接近满档的长读可以更差。
公开切片实测见 [docs/real-corpus.md](docs/real-corpus.md)。

## 解压路径

解压是压缩的严格逆过程：

1. 校验全局头（magic、版本、编解码器 ID、头部校验和）。
2. 逐帧读取：校验帧头 → Zstd 解压三路载荷 → 验证逻辑校验和 → 解码三路流 → 校验记录合法性。
3. 重建 FASTQ：`@{id}[ {comment}]\n{sequence}\n+\n{quality}\n`。
4. 校验 footer 总计和滚动校验和。

`verify` 命令走完全相同的解码和校验流程，只是不写 FASTQ 输出——
因此 CPU/内存成本与 `decompress` 同量级，不是常数时间的元数据检查。

## 最高压缩比专项（Phase 1/2 实验原型已实现；Phase 3 未实现）

当前文档的“质量值直通”“不重排”“不做参考”都是 fast 路径的设计取舍，不是对最高压缩比的
最终结论。若目标是 10×–20×+，需要把序列的跨 read 冗余（参考/伪基因组/重排）与质量值的
条件熵（上下文模型 + range coder/rANS）一起处理；完整设计、证据分级与本地实验见
[docs/max-ratio-design.md](docs/max-ratio-design.md)，路线图见 [docs/roadmap.md](docs/roadmap.md)。

已实现的部分：

- `fqc_ratio_probe`：Phase 0 字节/流级基线，见
  [tools/ratio_probe/README.md](tools/ratio_probe/README.md)；
- `fqc_quality_probe`：Phase 1 fqzcomp vs zstd 质量流探针，见
  [tools/quality_probe/README.md](tools/quality_probe/README.md) 与
  [质量 codec 基线](docs/max-ratio-baselines/2026-11-15-quality-codecs.md)；
- `fqc-max/v1` 实验容器与 `max-compress`/`max-decompress`/`max-verify`：复用 v2
  逻辑流，质量流 fqzcomp 分块自适应，独立 magic/校验和；实测 HiSeq 5.087× raw、
  NovaSeq 头部 8.165× raw、WGS 头部 7.790× raw，`max-verify` 与 `cmp` 通过。见
  [实验容器基线](docs/max-ratio-baselines/2026-11-15-max-container.md) 与
  [格式契约](openspec/changes/2026-11-15-max-ratio-phase1-container/design.md)。
- `fqc_ref_archive` + `fqc-max-ref/v1`：外部参考 + PAF 位置/方向/mismatch +
  fqzcomp 质量流；合成 5 Mb 参考 / 3× 双端 150 bp 上 R1/R2 约 21.05× raw，
  `verify`/`cmp` 通过。参考不计入、恒定 Q23、只接受纯 `<len>M`，是 Phase 2 机制验证，
  不是人类 30× WGS 结论。Phase 2.1 加入自适应 ID front coding（`idCodec=2`）：真实
  WGS/NovaSeq 头部 ID 探针约 −99%，随机 wgsim ID 自动回退 raw；人工结构化 ID 合成 R1
  331,553 B（53.385× raw），同样是机制验证。见
  [参考原型基线](docs/max-ratio-baselines/2026-11-15-ref-container.md)、
  [ID codec 基线](docs/max-ratio-baselines/2026-11-15-id-codec.md)、
  [格式契约](openspec/changes/2026-11-15-max-ratio-phase2-ref/design.md)、
  [ID codec 契约](openspec/changes/2026-11-15-max-ratio-phase2-id-codec/design.md)。

Phase 0 提案见
[openspec/changes/2026-11-14-max-ratio-phase0-benchmark](openspec/changes/2026-11-14-max-ratio-phase0-benchmark/proposal.md)，
质量 codec 提案见
[openspec/changes/2026-11-15-max-ratio-quality-codec](openspec/changes/2026-11-15-max-ratio-quality-codec/proposal.md)，
参考原型契约见
[openspec/changes/2026-11-15-max-ratio-phase2-ref](openspec/changes/2026-11-15-max-ratio-phase2-ref/design.md)。
它们都不改变 `fqc-sequential/v2` 或默认 CLI；`max-compress` 与 `fqc_ref_archive` 是
显式实验入口，不是默认 max-ratio，也不覆盖 `+` 行/CRLF/空 read 的逐字节边界。

## 设计取舍

| 选择 | 收益 | 代价 |
|---|---|---|
| 列式分离 | Zstd 字典效率大幅提升 | 编解码需要三趟遍历 |
| 2-bit 打包 | 序列 4:1 压缩，最大单一贡献 | 异常碱基需要额外列表 |
| Zstd level 1 | 高吞吐（>50 MiB/s 压缩） | 比高级别少 2–5% 压缩比 |
| 质量值不编码（fast 模式） | 实现简单，无损保证 | 压缩比瓶颈；max-ratio 专项另评估上下文编码 |
| 未压缩流校验和 | 检测 Zstd 静默错误 | 校验和计算在解压后的数据上 |
| 增量编码异常位置 | 避免 per-base 对象分配 | 异常密集时列表较长 |
