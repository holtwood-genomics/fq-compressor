# Agent Note: paired-end 参考归档与 R2 坐标 delta

Status: implemented

## Problem

`fqc-max-ref/v1` 之前只有单端路径：R1/R2 必须各写一个归档，R2 即使和 R1 落在同一
contig，也要重复存 contig id 和绝对 `targetStart`。真实 WGS 是 paired-end，这会白白
浪费 alignment 字节；同时，单端接口无法校验 R1/R2 是否一一配对。

先做一个最小扩展，不引入跨 mate 质量模型、ID 共享或 insert-size 建模：把同一模板的
两条 read 放进同一个 block，并只对 R2 的同 contig 坐标做相对编码。目标是验证真实数据上
是否有稳定、可解释的收益，而不是把原型包装成完整 paired-end 产品。

## Decision

`fqc-max-ref/v1` 增加 paired 扩展：

- 全局头 flags bit0 `PAIRED`；旧单端归档 flags=0，新解码器仍可读；
- `compress --mate R2 --mate-paf R2.paf` 用 `readRecordPair` 锁步读取，要求两端记录数
  一致，read ID 相同或只在末尾 `/1`、`/2` 上不同；不比较 comment；
- block 内按 `R1, R2, R1, R2...` 交错，paired block 记录数必须为偶数；
- per-read flags bit4 `MATE_DELTA` 只允许第二条 mate；只有 R1/R2 都 mapped 且同 contig
  时启用，存 `zigzag(targetStart_R2 - targetStart_R1)`，省略 contig id 和绝对坐标；
- 其他情况仍走绝对坐标；mismatch、CIGAR、clip、quality、ID 和序列重建语义不变；
- `decompress` 对 paired 归档输出交错 FASTQ，`decompress-paired --r1/--r2` 分离恢复；
- 解码拒绝奇数 paired block、没有 mapped first mate 的 delta、负向/正向越界；
- 旧单端布局、旧归档字节和 `verify` 兼容性保持不变。

## Alternatives considered

- **只做“单文件装两个独立归档”**：实现最简单，但只省文件头和少量容器开销，利用不到
  mate 对的位置关系；不采用。
- **先做跨 mate 共享 ID**：真实 R1/R2 的 ID 通常相同、comment 只差 `1/1`/`2/2`，理论上
  可以省一些字节；但真实 WGS 的 ID 流只有约 3.7 KB，收益远小于 alignment，且会改变
  ID 重建契约；先不做。
- **做 insert-size/方向约束的联合质量模型**：可能进一步压缩质量流，但需要建模
  read2/reverse 质量分布、跨块状态和更大验证矩阵；当前优先验证最小坐标收益；不采用。
- **直接做 BAM/CRAM sidecar 或内置 mapper**：更接近生产，但会引入参考版本、排序、
  索引和依赖问题；先用 external PAF 验证编码结构。
- **把 R2 绝对坐标改成相对坐标，但保留 contig id**：兼容性更简单，却只省几个字节；
  同 contig 时省略 contig id 才是主要收益。

## Consequences

- 收益：真实 `SRR14724532` R1/R2 各 298,974 reads + 完整 hg38 上，两个单端归档之和
  9,678,812 B（20.650× raw）降到 paired 8,825,785 B（22.646× raw），节省 853,027 B
  （8.813%）；alignment 流 4,939,473 → 4,094,927 B（−17.10%），
  `mate_deltas=277,453/298,974`（92.80%）。
- 质量流几乎中性：4,709,728 → 4,711,904 B（+0.046%）；ID 7,399 → 6,880 B
  （−7.0%）；容器开销 22,212 → 12,074 B。
- 代价：paired block 必须偶数和锁步，错误边界更多；解码需要保存前一条 mapped 坐标；
  没有跨 mate 质量/ID 模型，22.646× 不能解释成“完整 paired-end 压缩”。
- 边界：R1/R2 头部小切片，不是完整 30×；external reference/PAF 不计入主口径；没有
  ORA/Genozip 同数据复测；CRAM 对照是两个独立 alignment container 之和。
- 兼容性：新解码器可读旧单端归档；旧解码器会拒绝 paired flag 或 `MATE_DELTA`，属于
  实验格式的单向扩展。

## Verification

- `ref_archive_test` 新增 paired round-trip、R1/R2 分离恢复、交错恢复、ID mismatch、
  不完整请求和单端归档误用；14/14 cases passed。
- 全量 `clang-debug` / `clang-asan` 22/22 passed；`format-check` 通过。
- 真实 paired 归档 `verify`、`decompress-paired`、R1/R2 `cmp` 全部通过；重复压缩 SHA
  相同；旧 R1 单端归档仍可 `verify`，且新代码重压 R1 与旧归档逐字节一致
  （4,547,935 B，SHA `6f50a3146e018c33fffe6be3a21af9163b8ae1223d1b386f6f440507bb3b647a`）。
- 详细数据、SHA、命令和边界见
  [docs/max-ratio-baselines/2026-11-15-real-wgs-paired.md](../../../../docs/max-ratio-baselines/2026-11-15-real-wgs-paired.md)；
  契约见
  [openspec/changes/2026-11-15-max-ratio-paired-end/design.md](../../../../openspec/changes/2026-11-15-max-ratio-paired-end/design.md)。
