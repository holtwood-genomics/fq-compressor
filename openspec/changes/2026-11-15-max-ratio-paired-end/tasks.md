# Tasks: max-ratio-paired-end

## 1. Format and API

- [x] 1.1 全局头 paired flag 与兼容读取
- [x] 1.2 `MATE_DELTA` flag、zigzag 编解码与边界检查
- [x] 1.3 `compress --mate/--mate-paf`
- [x] 1.4 `decompress-paired --r1/--r2`
- [x] 1.5 paired/单端路径和输出路径校验

## 2. Tests

- [x] 2.1 paired round-trip + R1/R2 分离恢复
- [x] 2.2 交错恢复与 ID mismatch 拒绝
- [x] 2.3 不完整 paired 请求拒绝
- [x] 2.4 单端归档拒绝 `decompress-paired`
- [x] 2.5 旧单端归档兼容回归
- [x] 2.6 新代码重压旧 R1 归档，逐字节 `cmp` 与 SHA 一致

## 3. Real WGS validation

- [x] 3.1 固定 R2 40 MB gzip 前缀与 298,974 reads FASTQ
- [x] 3.2 生成 R2 minimap2 PAF
- [x] 3.3 R2 单端归档与 paired 归档
- [x] 3.4 `verify` + `decompress-paired` + `cmp`
- [x] 3.5 重复压缩 SHA
- [x] 3.6 gzip/zstd/FQC/CRAM 对照

## 4. Docs and gates

- [x] 4.1 paired 基线、SHA、命令和边界
- [x] 4.2 README/一页纸/CHANGELOG/OpenSpec/笔记同步
- [x] 4.3 clang-debug / clang-asan / format / notes 门禁
- [x] 4.4 `scripts/fetch_real_wgs_ref_bench.sh` 支持 `FQC_BENCH_PAIRED=1` 与可选 `FQC_BENCH_REPRO=1`

## 5. Explicit non-goals

- [ ] 5.1 完整 30× paired run
- [ ] 5.2 ORA/Genozip 同数据复测
- [ ] 5.3 跨 mate 质量/ID 联合模型
- [ ] 5.4 正式格式冻结
- [ ] 5.5 速度/内存优化
