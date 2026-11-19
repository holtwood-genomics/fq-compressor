# Verification: max-ratio-quality-codec

- Status: `Completed (research tool)`
- Ready to archive: `yes`（归档动作待用户复核）
- Date: 2026-11-15
- Scope: quality-stream probe only; no product format/CLI changes.

## Environment

- Repository: `holtwood-genomics/fq-compressor`
- Branch: `master`
- Working tree: 未提交；生产 `src/`/`include/`/`fqc-sequential/v2` 未改。
- Build/test: `clang-debug` 构建通过；全量 18/18 tests 通过；format-check 通过。
- htscodecs: `v1.6.7`，peeled commit `b9fc194f772e45bb0a1f44b08cbf8697a1384bae`，
  tarball SHA-256 `4f36d2800e97468435f724a4a0b26dfda6344b11dc16d3fa50b03f47b862e15a`。
- zstd: Conan `1.5.7`。

## Requirement -> Evidence Matrix

| Requirement | Scenario | Evidence | Result |
|---|---|---|---|
| fqzcomp round-trips quality bytes | HiSeq/NovaSeq | `fqc_quality_probe` 对每块 `memcmp` quality 值并核对长度 | passed |
| fqzcomp output is deterministic | Repeat same input | 重复运行压缩输出 SHA 一致（HiSeq `652c…`，NovaSeq `0f9f…`） | passed |
| Block size effect is measured | reads 分块曲线 | HiSeq 500–25k、NovaSeq 1k–303k blocks | passed |
| Quality gain ≥10% | Full and binned quality | HiSeq −17.7%，NovaSeq −22.9%，WGS 头部 −23.2%（vs zstd19） | passed |
| No product format change | Diff/CLI audit | 只新增 `tools/quality_probe` 与文档；`src/`/`include/` 未改 | passed |
| Malformed input fails closed | Length mismatch / invalid quality | 手工 fixture：长度不等返回非零；quality 值域检查 | passed |

## Commands

| Command | Status | Summary |
|---|---|---|
| `./tools/quality_probe/build.sh /tmp/htscodecs-v1.6.7` | passed | 构建 `build/quality-probe/fqc_quality_probe` |
| `build/quality-probe/fqc_quality_probe /tmp/fqc-exp/hiseq_R1.fastq --block-records 0` | passed | 25,000 records；fqz 558,189 B；zstd19 678,308 B |
| `build/quality-probe/fqc_quality_probe /tmp/fqc-exp/novaseq/DRR174812_R1.head.fastq --block-records 25000` | passed | 303,610 records；自适应 fqz 2,171,444 B；zstd19 2,859,644 B |
| `build/quality-probe/fqc_quality_probe /tmp/fqc-exp/wgs/SRR14724532_R1.head.fastq --block-records 0` | passed | 298,974 records；fqz 2,168,649 B；zstd19 2,823,235 B |
| `./scripts/build.sh clang-debug` | passed | 生产构建未受影响 |
| `./scripts/test.sh -p clang-debug` | passed | 18/18 tests |
| `./scripts/lint.sh format-check` | passed | clang-format-18 |
| `./scripts/notes.sh verify` | passed | 11 notes tree/format/links |
| `git diff --check` | passed | no whitespace errors |

## Notes

- 本文件记录的是当时的质量探针范围；它不代表最终 max-ratio 已实现，也没有 `--max-ratio`
  顶层开关。后续 Phase 1 实验容器 `fqc-max/v1` 由
  `openspec/changes/2026-11-15-max-ratio-phase1-container/` 单独记录。
- 归档影响 −16.6%/−16.3%/−15.5% 是“只替换质量流”的估算，未计入新容器/参数/索引/ID
  变化。
- NovaSeq 是 RNA-Seq 头部子集；WGS `SRR14724532` 是头部约 300k reads，完整 35× run
  未下载。
- paired-end read2/reverse、完整 run、吞吐/内存未验证。
- `./scripts/lint.sh lint clang-debug` 的 clang-tidy-18 `--exclude-header-filter` 兼容问题
  仍是既有工具链问题，未在本 change 中修复；`scripts/lint.sh` 已把外部依赖的
  `tools/quality_probe` 从 clang-tidy 文件集排除，格式检查仍覆盖它。
