# Design: `fqc-max-ref/v1` 参考压缩原型

- 状态：实验格式，追溯补契约；不是最终对外格式
- 日期：2026-11-15
- 实现：`tools/ref_archive/ref_archive.cpp`、`tools/ref_archive/main.cpp`
- 测试：`tests/tools/ref_archive_test.cpp`
- 基线：[docs/max-ratio-baselines/2026-11-15-ref-container.md](../../../docs/max-ratio-baselines/2026-11-15-ref-container.md)

## 1. 目标与边界

目标是在不改变 fast 路径的前提下，验证“外部参考 + PAF 位置/差异 + 质量上下文”能否
形成可校验、可解压、可追溯的归档。它只覆盖 Phase 2 的第一种输入模式：**外部参考 +
PAF sidecar**。内置 mapper、BAM sidecar、参考嵌入、clip/indel 都是后续工作。

## 2. 输入契约

`compress` 需要三个输入：

1. FASTQ：生产 `FastqParser` 读取，canonical LF 输出；当前不支持 gzip/stdin。
2. reference FASTA：纯文本，`>` header 后第一个空白前为 contig 名；序列按行拼接；
   contig 名必须非空且唯一，序列必须非空；不支持 gzip。
3. PAF：至少 12 列；按 query name 取 `tp:A:P` 的 primary 比对中 MAPQ 最高者；
   必须带 `cg:Z:`。当前接受且仅接受：

```text
queryStart == 0
queryEnd == readLength
targetEnd - targetStart == readLength
cg:Z:<digits>M
```

不满足条件的 read 不报错，回退 literal。PAF 里的 `NM:i:` 只用于解析，实际 mismatch
数由 reference 与 read 重新计算；超过 `--max-mismatches` 回退 literal。负链 read 先做
reverse complement，再与 reference slice 比较，存 orientation bit。

## 3. 容器布局

所有整数小端；varint 为 LEB128；字符串为 `varint(len)+bytes`。

```text
global header (52 bytes + contig table)
  magic[8]          = "FQCMR1\r\n"
  version u16       = 1
  headerSize u16    = 52 + tableBytes
  flags u32         = 0
  contigCount u32
  referenceBases u64
  referenceDigest u64 (XXH64; prototype only)
  tableBytes u64
  headerChecksum u64 (XXH64 over header with this field zero)
  contig table:
    per contig: varint(nameLen) + name + varint(sequenceLen)
```

`referenceDigest` 覆盖 contig 名、序列和长度；编码/解码两端都从 reference FASTA 重新
计算。当前实现使用 XXH64，**不是**最终设计要求的 SHA-256；它只能发现意外不匹配，
不能抵抗恶意构造。

```text
block
  magic u32         = "FQMR" little-endian (0x524D5146)
  headerSize u32    = 88
  blockId u32       = 0-based
  recordCount u32   > 0
  rawIdsSize u64
  idsSize u64
  rawQualitiesSize u64
  qualitiesSize u64
  rawAlignmentsSize u64
  alignmentsSize u64
  blockChecksum u64
  idCodec u8        = 1 (zstd)
  qualityCodec u8   = 0 (zstd) / 1 (fqzcomp)
  reserved u16      = 0
  headerChecksum u64 (XXH64 over first 76 bytes)
  payloads: ids | qualities | alignments
```

- ID 逻辑流：`varint(recordCount)`，每条 `varint(idLen)+id+varint(commentLen)+comment`；
- quality 逻辑流：`varint(recordCount)`，每条 `varint(len)+ASCII quality`；
- alignment 流：每条 `flags u8` + `varint(readLen)`：
  - mapped：`varint(contigId)+varint(targetStart)+varint(mismatchCount)`，随后每个
    mismatch 为 `varint(delta)+u8(baseCode)`；flags bit0=1，bit1=reverse；
  - literal：原始 sequence bytes；flags bit2=1，bit0/bit1 必须为 0；
- 每块三路原始逻辑流按 streaming XXH64 依次覆盖 `ids || qualities || alignments`；
  对应实现中的 `blockChecksum(ids, qualities, alignments)`；
- footer：

```text
footer
  magic u32         = "FQRF" little-endian (0x46525146)
  footerSize u32    = 40
  blockCount u64
  recordCount u64
  totalBases u64
  globalChecksum u64 = advanceGlobalChecksum over blockChecksum in order
```

footer 后必须 EOF；任何尾随字节报格式错误。空 FASTQ 合法：只有 header + footer，
0 blocks。

## 4. 编解码流程

压缩：

1. 加载 reference 与 PAF，校验路径互异、reference 形状；
2. 逐块读 FASTQ（按 `--block-bases`，默认 4,000,000 bases）；
3. 每条 read 尝试 PAF 映射；命中条件见 §2；
4. 生成 ids/qualities/alignments 三路原始流；
5. ids/alignments 用 zstd19；quality 同时尝试 fqzcomp 策略 0/1 与 zstd19，取更小者；
6. 写 block header + 三路 payload；更新块/全局校验和；
7. 写 footer，flush。

解压/验证：

1. 读取 global header 与 contig table，校验 header checksum；
2. 加载外部 reference，逐 contig 核对名称/长度与 digest；不匹配 fail closed；
3. 逐块读取 header/payload，校验 header checksum 与尺寸上限；
4. zstd/fqzcomp 解压，校验块 checksum；
5. 重建 mapped/literal read，校验坐标、mismatch 顺序、长度、尾部字节；
6. 累积全局 checksum，校验 footer 计数与 checksum；
7. `decompress` 写 canonical LF FASTQ；`verify` 不写文件。

## 5. 拒绝行为

以下情况必须失败，不产生“部分成功”：

- magic/version/headerSize 不合法；contig 表截断、空名、重复名、空序列；
- reference digest/contig 表不一致；
- block magic/headerSize/blockId/recordCount 不合法；
- 任一 stream 超过 512 MiB 或长度溢出；
- zstd/fqzcomp 解压失败或输出长度不符；
- 块 checksum、footer 计数、全局 checksum 不符；
- mapped 坐标越界、mismatch 位置越界/不递增、base code >4；
- literal/mapped flags 同时设置、反向 literal、长度与 quality 不一致；
- footer 后尾随字节；截断归档；
- 输出路径等于输入/参考（compress）或 archive/参考（decompress）。

当前原型**没有**原子输出：写失败可能留下部分目标文件；这是实验工具边界，不是最终
产品行为。

## 6. 质量与 ID

- quality 复用 `format::compressFqzQuality` / `decompressFqzQuality`；fqzcomp 输入是
  Phred+33 减 33 后的无分隔值，长度由 slice 携带；解码重建 v2 风格 quality 逻辑流；
- 每块独立训练，块目标 4M bases；zstd 回退只在 fqzcomp 不更小/失败时启用；
- ID 仍是 raw id/comment + zstd19，没有 token/delta/front-coding；这是当前归档的主要
  体积来源之一，也是后续最明确的压缩比改进项；
- 不支持 `+` 行内容/CRLF/空 read 的字节保真；生产 parser 会规范化。

## 7. 与最终 `max-ref` 的差距

- 没有内置 mapper，必须外部 PAF；
- 没有 BAM/CRAM sidecar；
- 没有 clip/indel 编码；纯 `<len>M` 之外全部 literal；
- 外部参考不计入归档，没有 `--embed-reference`；
- 参考身份是 XXH64，不是 SHA-256；
- 没有 paired-end 联合、ID token、原子输出、内存预算、并发；
- 没有 30× 人类 WGS 验收，只有合成数据机制验证。

这些差距必须在正式格式/CLI 之前逐项补齐或明确缩范围。
