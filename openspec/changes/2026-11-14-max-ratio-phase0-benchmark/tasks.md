# Tasks: max-ratio-phase0-benchmark

> 状态：**Phase 0 探针已实现；数据集扩展与最终归档待补**。生产格式/CLI 未改动。

## 1. Dataset baseline

- [x] 1.1 建立数据集 manifest：来源、accession/URL、下载日期、SHA-256、read 数、read 长度、
  覆盖度、质量分布摘要；见 `docs/max-ratio-baselines/datasets.md` 与 `datasets.json`
- [x] 1.2 固定本地 HiSeq 小样本快照并复核设计文档 §4.1 的 SHA-256（2026-11-15）
- [x] 1.3 选定 NovaSeq 分箱质量与深覆盖 WGS 数据集：`DRR174812` R1 头部子集与
  `SRR14724532` R1 头部子集/完整 run manifest；完整 35× run 未下载

## 2. Ratio probe

- [x] 2.1 设计只读原始字节扫描器：保留 `+` 行、CRLF/LF、空 read 与异常字节
- [x] 2.2 实现开发目标 `fqc_ratio_probe`（不安装、不进入产品 CLI）
- [x] 2.3 输出 record/bases/N/quality、按流原始字节、packed sequence、gzip/zstd 基线
- [x] 2.4 畸形输入 fail closed；重复运行字节数稳定（`ratio_probe_test`）
- [x] 2.5 为现有 `FastqParser` 与原始字节层分别记录边界，禁止混用口径

## 3. Baselines and report

- [x] 3.1 跑当前 FQC（固定 commit/profile/frame/workers/quality level）并记录归档 SHA
- [x] 3.2 跑 zlib gzip level 6、zstd level 19；外部工具仅在可复现时运行（本次未运行外部工具）
- [x] 3.3 输出 raw/gzip/归档/bpb 四类口径与流级占比
- [x] 3.4 把结果写入 `docs/max-ratio-design.md` §12.6 与 `docs/max-ratio-baselines/2026-11-15-hiseq.md`

## 4. Documentation and verification

- [x] 4.1 更新 `docs/max-ratio-design.md` 状态、证据矩阵与未验证边界
- [x] 4.2 更新 `docs/roadmap.md` Phase 0 状态、`CHANGELOG.md` 与 ADR/notes 链接
- [x] 4.3 运行 `./scripts/notes.sh verify`、`git diff --check` 与适用的构建/测试门禁
- [x] 4.4 在 `verification.md` 填入命令、退出码、结果与未运行项
