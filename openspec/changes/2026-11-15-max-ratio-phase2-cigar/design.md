# Design: `fqc-max-ref/v1` clip/indel CIGAR 扩展

- 状态：实验格式的向后兼容扩展；不是最终格式
- 日期：2026-11-15
- 实现：`tools/ref_archive/ref_archive.cpp`
- 测试：`tests/tools/ref_archive_test.cpp`
- 基线：[2026-11-15-ref-cigar.md](../../../docs/max-ratio-baselines/2026-11-15-ref-cigar.md)

## 1. 基础格式

复用 Phase 2 的 global header、contig table、block、footer、checksum、ID codec 和
quality codec，只扩展 mapped read 的 alignment 记录。基础布局见
[Phase 2 design](../2026-11-15-max-ratio-phase2-ref/design.md)。

## 2. CIGAR 输入契约

PAF 必须带 `cg:Z:`。解析器接受：

| 操作 | 语义 | query 消费 | target 消费 |
|---|---|---:|---:|
| `M` / `=` / `X` | 参考与 query 对齐，实际差异重算 | 是 | 是 |
| `I` | query 多出的碱基 | 是 | 否 |
| `D` | 参考多出的碱基 | 否 | 是 |

`S/H/N/P` 不支持。CIGAR 必须非空、操作长度非零、操作数 ≤ 256，且：

```text
sum(M/I query consumption) == queryEnd - queryStart
sum(M/D target consumption) == targetEnd - targetStart
0 <= queryStart < queryEnd <= readLength
targetStart < targetEnd <= reference contig length
```

不满足时整条 read 回退 literal，不写部分编辑脚本、不产生半成功。

## 3. 每-read 布局

沿用现有 flags：

| bit | 含义 |
|---:|---|
| 0 | mapped |
| 1 | reverse |
| 2 | literal |
| 3 | extended mapping（clip 或 I/D） |

flags 的高 4 位必须为 0。literal 不可带 reverse/extended；mapped 与 literal 互斥。

纯全长 `<len>M` 仍使用旧布局：

```text
flags u8 + varint(readLen)
varint(contigId) + varint(targetStart)
varint(mismatchCount) + (varint(delta) + u8(baseCode))*
```

扩展 mapped 记录：

```text
flags u8 + varint(readLen)
varint(contigId) + varint(targetStart)
varint(queryStart) + varint(queryEnd)
varint(operationCount)
per operation:
  varint(length) + u8(code)       # 0=M/=/X, 1=I, 2=D
  if code == I: length * u8(baseCode)
varint(leftClipLen) + leftClipLen * u8(baseCode)
varint(rightClipLen) + rightClipLen * u8(baseCode)
varint(mismatchCount)
per mismatch:
  varint(delta) + u8(baseCode)
```

- `mismatch` 位置是 **aligned query** 内的累计位置，跨 `I` 继续累计；delta 相对前一个
  位置，第一条从 0 开始，必须严格递增。
- `leftClip`/`rightClip` 按原始 read 方向保存；对于负链，解码先在 oriented aligned
  query 上重建，再 reverse complement，最后拼回原始左右 clip。
- `I` 的 base code 存在操作之后；`D` 只消费参考，不存序列。
- `baseCode`：0=A、1=C、2=G、3=T、4=N；只允许大写 `ACGTN` 进入 mapped 路径。
- 扩展布局的 `leftClipLen` 必须等于 `queryStart`，`rightClipLen` 必须等于
  `readLength - queryEnd`。

## 4. 编码流程

1. 用 `queryStart/queryEnd` 截出原始 read 的 aligned 中段；负链先对中段做 reverse
   complement。
2. 解析 CIGAR，校验 query/target 消费量与 PAF 坐标一致。
3. 遍历操作：`M/=/X` 逐位比较参考和 aligned query，记录替换；`I` 追加 base code；
   `D` 跳过参考。
4. 替换数超过 `--max-mismatches` 时回退 literal。
5. 纯全长单 M 用旧布局；否则设置 bit3，写 CIGAR、insertion、clip、mismatch。

## 5. 解码流程

1. 校验 flags/readLen/quality 长度。
2. 读 contigId/targetStart；扩展记录继续读 query 范围、操作表、insertion、左右 clip。
3. 按操作表从参考和 insertion 重建 oriented aligned query；所有 target 访问必须边界
   检查。
4. 应用 mismatch，校验位置顺序和 query 范围。
5. 负链 reverse complement aligned query，再拼 left clip + aligned + right clip。
6. 校验最终长度等于 readLen，继续原有块 checksum/footer 流程。
7. 任何未知操作、长度为零、坐标越界、clip 长度不符、insertion 多余、mismatch 乱序
   均返回格式错误，不输出部分 FASTQ。

## 6. 兼容性

- 旧归档没有 bit3，按旧布局解码；纯全长 M 的新编码也仍使用旧布局。
- 新解码器可读旧 Phase 2 归档；旧工具会拒绝带 bit3 的新归档，这是实验工具的单向扩展。
- `fqc-max-ref/v1` magic/version/block header/footer 不变；checksum 仍覆盖原始
  `ids || qualities || alignments` 逻辑流。
- 正式格式仍必须重新冻结 magic/version/header，并重新评估 CIGAR 操作集。

## 7. 明确不包含

- `S/H/N/P`、spliced alignment、长读；
- BAM/CRAM sidecar、内置 mapper；
- 参考嵌入、SHA-256、paired-end 联合；
- `+` 行/CRLF/空 read 的原始字节保真；
- 速度/内存门禁与真实 30× WGS 验收。
