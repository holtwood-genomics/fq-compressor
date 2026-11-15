# Tasks: max-ratio-phase1-container

## 1. Format and container

- [x] 1.1 定义 `fqc-max/v1` global header / block header / footer 与独立 magic
- [x] 1.2 复用 v2 三流逻辑布局与 `logicalChecksum`，暴露只读解码入口
- [x] 1.3 实现块级/全局 XXH64、尾部字节拒绝、空归档
- [x] 1.4 实现单块大小/内存预算预检与 fail-closed 错误

## 2. Quality codec and dependency

- [x] 2.1 接入 htscodecs `v1.6.7` fqzcomp（策略 0/1 自适应）
- [x] 2.2 每块与 zstd19 比较，严格更小才选 fqzcomp
- [x] 2.3 vendor 最小 C 子集，固定 tag/commit/tarball SHA 与 BSD-3-Clause
- [x] 2.4 CMake 私有静态库 `fqc_fqzcomp`；Conan `exports_sources` 包含 `third_party/*`

## 3. CLI and engine

- [x] 3.1 `max-compress` / `max-decompress` / `max-verify` 与别名
- [x] 3.2 临时文件 + rename，`-f` 覆盖语义，stdin/stdout 支持
- [x] 3.3 profile 自动检测与显式覆盖；块按 bases 切分
- [x] 3.4 默认 CLI、v2 格式与 fast 路径不变

## 4. Tests and measurements

- [x] 4.1 `max_archive_test`：跨块 round-trip、空归档、截断、损坏块、未知 magic
- [x] 4.2 e2e：max-compress → max-verify → max-decompress → cmp；空文件
- [x] 4.3 HiSeq / NovaSeq 头部 / WGS 头部实测归档与输入/输出 SHA
- [x] 4.4 `max-verify` + `cmp` 三个归档，重复压缩 SHA 一致

## 5. Documentation and gates

- [x] 5.1 新增实测基线 `docs/max-ratio-baselines/2026-11-15-max-container.md`
- [x] 5.2 新增 implemented Agent Note，记录 vendor 与格式取舍
- [x] 5.3 更新设计、路线图、开发历程、README、ALGORITHM、ARCHITECTURE、CHANGELOG
- [x] 5.4 构建、全量测试、format-check、notes verify、`git diff --check`
