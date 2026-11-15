# Tasks: max-ratio-quality-codec

## 1. Codec and dataset baseline

- [x] 1.1 固定 htscodecs `v1.6.7` tag/commit/tarball SHA 与许可证边界
- [x] 1.2 固定 HiSeq、NovaSeq `DRR174812` 头部子集与 WGS `SRR14724532` manifest
- [x] 1.3 记录输入 SHA、quality 统计与当前 FQC 归档基线

## 2. Quality probe

- [x] 2.1 实现 quality-only FASTQ 读取与 Phred+33 值域检查
- [x] 2.2 接入 fqzcomp 策略 0–3 与自适应 0/1，逐块 round-trip 校验
- [x] 2.3 接入 zstd19 对照与 `fqc-quality-probe/v1` JSON
- [x] 2.4 通过外部 htscodecs 源码树构建，不进入产品 CMake/CLI
- [x] 2.5 覆盖 LF/CRLF、空 read、literal `+` 行与畸形输入的手工 fixture

## 3. Measurements

- [x] 3.1 HiSeq 整文件与 reads 分块曲线
- [x] 3.2 NovaSeq 头部整文件、reads 分块与四等分曲线
- [x] 3.3 记录 fqzcomp 重复运行输出 SHA（确定性）
- [x] 3.4 估算质量流替换对当前 FQC 归档的影响，并标注非实测边界

## 4. Documentation and verification

- [x] 4.1 新增 `docs/max-ratio-baselines/datasets.md` / `datasets.json`
- [x] 4.2 新增 `docs/max-ratio-baselines/2026-11-15-quality-codecs.md`
- [x] 4.3 更新设计、路线图、ADR、CHANGELOG 与 Agent Note
- [x] 4.4 运行构建、全量测试、format-check、notes verify 与 `git diff --check`
