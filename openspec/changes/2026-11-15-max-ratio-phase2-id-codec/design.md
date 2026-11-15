# Design: `fqc-max-ref/v1` 自适应 ID front coding

- 状态：实验格式的向后兼容扩展；不是最终格式
- 日期：2026-11-15
- 实现：`tools/ref_archive/ref_archive.cpp`
- 测试：`tests/tools/ref_archive_test.cpp`
- 基线：[2026-11-15-id-codec.md](../../../docs/max-ratio-baselines/2026-11-15-id-codec.md)

## 1. 基础格式

本 change 复用 Phase 2 的 global header、contig table、block、footer、checksum 和
PAF/reference 输入契约，只扩展 block 的 ID payload 编码。基础布局见
[Phase 2 design](../2026-11-15-max-ratio-phase2-ref/design.md)。

## 2. 两种 ID 逻辑流

raw（`idCodec=1`）：

```text
varint(recordCount)
per record:
  varint(idLen) + id
  varint(commentLen) + comment
```

front-coded（`idCodec=2`）：

```text
varint(recordCount)
per record:
  varint(commonPrefixLen(id, previousId))
  varint(idSuffixLen)
  idSuffix
  varint(commonPrefixLen(comment, previousComment))
  varint(commentSuffixLen)
  commentSuffix
```

第一条记录的前一条 id/comment 视为空串。编码端不做跨块状态；每条 block 从空串开始。

## 3. 自适应选择

编码端对同一 block 同时生成 raw 和 front-coded 两路逻辑流，分别 zstd19：

```text
rawPayload   = zstd19(rawStream)
frontPayload = zstd19(frontStream)
if len(frontPayload) < len(rawPayload):
    idCodec = 2
    payload = frontPayload
    codecIdsSize = len(frontStream)
else:
    idCodec = 1
    payload = rawPayload
    codecIdsSize = 0
```

不使用全局开关；每个 block 独立选择。这样结构化 ID 走 front，随机 ID 自动退回 raw。

## 4. Header 字段

现有 block header 的尾部 4 字节保留位定义为 `codecIdsSize: u32`：

- `idCodec=1` 时必须为 0；旧归档的该字段本来就是 0，继续可读；
- `idCodec=2` 时必须大于 0，表示 front-coded 逻辑流的未压缩长度；
- `rawIdsSize` 始终表示**解码后的 raw ID 逻辑流长度**；
- `idsSize` 表示实际压缩 payload 长度；
- header checksum 覆盖包含 `codecIdsSize` 的完整 88 字节（checksum 字段自身置零）。

`codecIdsSize` 用 u32 足够：block 流上限是 512 MiB。

## 5. 完整性与解码

- block checksum 仍按 `ids || qualities || alignments` 的**raw 逻辑流**计算；
- 解码先从 payload 解压出 codec 逻辑流：
  - `idCodec=1`：直接作为 raw 流；
  - `idCodec=2`：按 front-coded 规则重建 raw 流；
- 重建后必须满足 `raw.size() == rawIdsSize`，否则格式错误；
- 然后校验 block checksum，再交给原有 `decodeBlock`；
- front-coded 解码必须拒绝：
  - 前缀长度大于上一条字段长度；
  - 后缀长度越过当前 payload；
  - 记录数不符；
  - 尾部多余字节；
  - 重建后 raw 长度不符。

## 6. 兼容性

- 旧 `idCodec=1` 归档不需要迁移；尾部 0 被解释为“无 front payload”；新解码器读取旧
  归档是兼容的。
- 反向不兼容：旧版 `fqc_ref_archive` 只接受 `idCodec=1`，会拒绝使用 `idCodec=2` 的新
  归档；这是实验工具的一次单向格式扩展，不是双向兼容承诺。
- 新编码器对随机 wgsim ID 仍选择 raw，输出与改动前逐字节一致；
- `fqc-max-ref/v1` magic/version 不变。这个扩展只用于实验工具；正式格式仍必须重新冻结
  magic/version/header 布局。

## 7. 明确不包含

- 数字 token/delta、ID 字段字典、跨块模型；
- paired-end 联合 ID 编码；
- CRLF/`+` 行/空 read 的逐字节保真；
- 速度/内存门禁。
