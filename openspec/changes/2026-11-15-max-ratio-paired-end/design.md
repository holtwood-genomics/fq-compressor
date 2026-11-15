# Design: paired-end 参考归档小切片

## 1. 容器扩展

仍使用 `FQCMR1\r\n`、version 1 和 88 字节 block header：

- 全局头 flags bit0 `PAIRED`；
- paired block 内记录顺序为 `R1, R2, R1, R2...`，记录数必须为偶数；
- 每条记录仍保留自己的 ID/quality/alignment；
- 旧单端 flags=0，布局与字节不变。

## 2. 配对校验

`compress` 用 `readRecordPair` 锁步读取：

- 两端记录数必须一致；
- read ID 必须相同，或只在末尾 `/1`、`/2` 上不同；
- 不校验 comment，因为 CASAVA/ENA 常用 `1:N:...` / `2:N:...` 或 `1/1` / `2/2`。

## 3. R2 坐标 delta

per-read flags bit4 `MATE_DELTA`：

- 仅第二条 mate 可用；
- 只有 R1/R2 都 mapped、同一 contig 时启用；
- 存 `zigzag(targetStart_R2 - targetStart_R1)`，省略 `contigId` 和绝对坐标；
- 其他情况仍存 `contigId + targetStart`；
- 解压拒绝：奇数 paired block、无 mapped first mate 的 delta、负向/正向越界。

delta 只改变坐标表示，不改变 mismatch、CIGAR、clip、quality、ID 或序列重建语义。

## 4. 恢复

- `decompress` 对 paired 归档输出交错 FASTQ；
- `decompress-paired` 按记录奇偶写入 R1/R2 两个输出；
- `verify` 完整解码所有块，不写 FASTQ；
- 解压仍只需要同一 external reference，不需要 PAF。

## 5. 验收

- 真实 `SRR14724532` R1/R2 各 298,974 reads + 完整 hg38；
- 主口径 external reference/PAF 不计入；
- 与两个单端归档之比较体积；
- `verify` + `decompress-paired` + `cmp`；
- 重复压缩 SHA；
- 旧单端归档兼容。

## 6. 非目标

完整 30×、paired 质量/ID 联合模型、insert-size 建模、BAM sidecar、内置 mapper、
`S/H/N/P`、参考嵌入、SHA-256、正式格式、速度/内存门禁。
