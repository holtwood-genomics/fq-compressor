# Design: max-ratio-phase1-container

## Goal

定义一个独立、可校验、可复现的 Phase 1 容器，把质量 codec 收益落成实际归档；格式边界
必须让“实验容器”与 `fqc-sequential/v2` 明确分离。

## Format

所有整数小端。

### Global header（32 B）

| 偏移 | 长度 | 字段 |
|---:|---:|---|
| 0 | 8 | magic `FQCMX1\r\n` |
| 8 | 2 | version = 1 |
| 10 | 2 | header size = 32 |
| 12 | 4 | flags = 0 |
| 16 | 1 | dataset profile（illumina/ont/pacbio-hifi/pacbio-clr） |
| 17 | 1 | ID codec = 1（zstd） |
| 18 | 1 | sequence codec = 1（2-bit packed + zstd） |
| 19 | 1 | header quality codec = 0（mixed/per-block） |
| 20 | 4 | reserved = 0 |
| 24 | 8 | XXH64(header[0..24]) |

### Block header（88 B）

| 偏移 | 长度 | 字段 |
|---:|---:|---|
| 0 | 4 | block magic `FQMB`（little-endian `0x424D5146`） |
| 4 | 4 | header size = 88 |
| 8 | 4 | block id（从 0 递增） |
| 12 | 4 | record count（>0） |
| 16 | 8 | raw ID bytes |
| 24 | 8 | compressed ID bytes |
| 32 | 8 | raw sequence bytes（v2 逻辑流） |
| 40 | 8 | compressed sequence bytes |
| 48 | 8 | raw quality bytes（v2 逻辑流） |
| 56 | 8 | compressed quality bytes |
| 64 | 8 | logical checksum（v2 三流 chained XXH64） |
| 72 | 1 | quality codec：0=zstd，1=fqzcomp |
| 73 | 1 | fqzcomp strategy（0–3；zstd 时为 0） |
| 74 | 2 | reserved = 0 |
| 76 | 8 | XXH64(header[0..76]) |
| 84 | 4 | reserved = 0 |

Block header 后依次是三段 payload：ID、sequence、quality。每段长度由块头给出；读取时
必须同时满足单块上限与内存预算。

### Footer（40 B）

| 偏移 | 长度 | 字段 |
|---:|---:|---|
| 0 | 4 | footer magic `FQMF`（`0x464D5146`） |
| 4 | 4 | footer size = 40 |
| 8 | 8 | block count |
| 16 | 8 | record count |
| 24 | 8 | total bases |
| 32 | 8 | 全局滚动 XXH64 |

footer 之后不允许尾随字节。空归档 = global header + footer，无 block。

## Logical streams

- ID/sequence/quality 的**未压缩**布局复用 v2 `encodeFrame` 的三流：
  - ID：`varint(count)` + 每条 `varint(idLen)+id+varint(commentLen)+comment`；
  - sequence：`varint(count)` + 每条 packed 2-bit 序列 + 异常碱基列表；
  - quality：`varint(count)` + 每条 `varint(len)+ASCII quality`。
- `logicalChecksum` 与 v2 相同：三流依次 chained XXH64；块头存储该值。
- ID 与 sequence 用独立 zstd 流（默认 level 19）；quality 先尝试 fqzcomp，若其字节数
  严格小于 zstd19 则使用 fqzcomp，否则回退 zstd19。

## Quality codec

- fqzcomp 输入：每条 read 的 ASCII quality 减 33，按记录拼接；`fqz_slice.len` 给长度，
  `flags=0`；`fqz_compress(vers=4, strat, gp=nullptr)`。
- 每块尝试 strategy 0/1，取较小者；块头记录 strategy。zstd 回退记录 codec=0。
- 解码时 `fqz_decompress` 返回每条长度；必须校验总长度、每个 quality 值 ≤93，并重建
  与 v2 完全一致的 quality 逻辑流后再算 `logicalChecksum`。

## CLI and limits

- `max-compress -i <fastq|-> -o <fqmx> [--profile auto|illumina|ont|pacbio-hifi|pacbio-clr]`
  `[--block-bases 4000000] [--max-block-mib 128] [--zstd-level 19] [-f]`
- `max-decompress -i <fqmx> -o <fastq|-> [-f]`
- `max-verify <fqmx>`
- 默认 `--memory-limit 16384` MiB；单块上限 `min(--max-block-mib, (memory-limit-16MiB)/8)`。
- 输出使用临时文件 + rename，失败不留下半成品；`-f` 才覆盖已存在输出。

## Lossless boundary

当前实现复用生产 `FastqParser`，因此：

- 输入 CRLF 会被规范化为 LF；
- `+` 行内容被丢弃，解压输出固定为 `+`；
- 空 sequence 被 parser 拒绝；
- 输出总是 canonical LF FASTQ。

所以本容器只对“parser 可接受、且原始字节已是 canonical LF”的输入做逐字节 round-trip。
最终 max-ratio 必须实现独立的 byte-preserving scanner 与逐记录行尾/`+` 行元数据；这不是
本 change 的完成项。

## Why not reuse v2 magic

复用 v2 magic 会让旧解码器把 `fqc-max/v1` 误判为 v2，产生误导性的“未知 codec/帧损坏”
错误；独立 magic 让版本、codec 与失败模式清晰，也避免实验格式污染兼容性契约。

## Vendor boundary

- 上游：htscodecs `v1.6.7`，peeled commit `b9fc194f772e45bb0a1f44b08cbf8697a1384bae`，
  tarball SHA-256 `4f36d2800e97468435f724a4a0b26dfda6344b11dc16d3fa50b03f47b862e15a`。
- 只 vendor `fqzcomp_qual.c/h`、`utils.c/h`、`varint.h`、`c_simple_model.h`、
  `c_range_coder.h` 与最小 `config.h`；其余文件逐字节保持上游。
- 许可证 BSD-3-Clause，`LICENSE.md` 随源码保留。
- CMake 构建私有 `fqc_fqzcomp` 静态库；C 源码关闭项目警告集，不安装头文件。
