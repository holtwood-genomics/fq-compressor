# Verification: max-ratio-phase0-benchmark

- Status: `Completed (measurement scope)`
- Ready to archive: `yes`（归档动作待用户复核；本文件不代表 max-ratio 已实现）
- Date: 2026-11-15
- Scope: Phase 0 only; no product format/CLI changes.

## Environment

- Repository: `open-genomics/fq-compressor`
- Branch: `master`
- Working tree state at verification: Phase 0 探针、数据集 manifest 与测试已实现，尚未提交；
  生产格式/CLI 未改。
- Build/test: `clang-debug` 构建通过；全量 18/18 tests 通过；format-check 通过。
- Dataset scope: HiSeq 全质量 + NovaSeq `DRR174812` R1 头部 21,110,784 B 子集 +
  WGS `SRR14724532` R1 头部 21,110,784 B 子集已本地固定；WGS 完整 35× run 未下载。

## Manual pre-check (2026-11-14)

这不是 Phase 0 探针验收，只用于确认现有 FQC baseline 的命令/字节数/SHA 与设计文档一致：

| Command | Result |
|---|---|
| `fqc compress -i /tmp/fqc-exp/hiseq_R1.fastq -o /tmp/fqc-exp/repro-64.fqc --frame-mib 64 --parse-workers 4 --quality-level 1 --force` | 1,634,437 B; SHA-256 `19bdd931f2332376209c512d6c18228058949d8239131e8b626c9be528236b1c`，与设计文档 §4.1 一致 |
| 同上，`--frame-mib 4` | 1,635,111 B（+674 B，约 +0.04%） |

## Requirement -> Evidence Matrix

| Requirement | Scenario | Evidence | Result |
|---|---|---|---|
| Dataset identity is fixed | Re-run on same data | HiSeq、NovaSeq 头部、WGS 头部快照与 WGS 完整 run manifest 均记录在 `docs/max-ratio-baselines/datasets.md`；完整 run 未下载 | passed (head-subset scope) |
| Probe counts are deterministic | Repeat same input | `ratio_probe_test.BaselinesAreDeterministic`; repeated JSON/TSV byte counts stable | passed |
| Stream accounting is separated | Inspect report | `raw_streams.*.content/line_endings/total` + accounting invariant test | passed |
| Parser boundary is explicit | `+`/CRLF/empty read fixtures | `ratio_probe_test` covers CRLF, empty read, `+` literal, missing final newline | passed |
| Current FQC baseline is reproducible | Fixed commit/profile | HiSeq 64 MiB archive 1,634,437 B；NovaSeq 头部 64 MiB archive 16,344,935 B，SHA 记录于质量 codec 基线 | passed |
| No production format change | Diff/CLI audit | `src/`, `include/`, `fqc-sequential/v2` unchanged; only `tools/` + tests/build/docs | passed |
| Malformed input fails closed | Truncated/garbled FASTQ | `ratio_probe_test.RejectsMalformedInput` covers five cases | passed |

## Commands

| Command | Status | Summary |
|---|---|---|
| `./scripts/build.sh clang-debug` | passed | Clang 21.1.8/libc++; probe and tests built |
| `./scripts/test.sh -p clang-debug` | passed | 18/18 tests |
| `./scripts/lint.sh format-check` | passed | clang-format-18 |
| `./build/clang-debug/tools/fqc_ratio_probe -i /tmp/fqc-exp/hiseq_R1.fastq --json` | passed | 25,000 records; raw/gzip/zstd baselines recorded |
| `./build/clang-debug/tools/fqc_ratio_probe -i /tmp/fqc-exp/novaseq/DRR174812_R1.head.fastq --zstd-level 1 --json` | passed | 303,610 records；4 个质量符号；quality zstd1 4,827,256 B |
| `./build/clang-debug/src/fqc compress ... --frame-mib 64 ...` | passed | HiSeq 1,634,437 B；NovaSeq 头部 16,344,935 B；WGS 头部 16,811,413 B；SHA 已记录 |
| `./scripts/notes.sh verify` | passed | 11 notes tree/format/links |
| `git diff --check` | passed | no whitespace errors |

## Notes

- 本文件记录的是 Phase 0 测量底座范围；当时没有 `--max-ratio`、新 magic 或生产格式改动。
  后续 Phase 1 实验容器 `fqc-max/v1` 由
  `openspec/changes/2026-11-15-max-ratio-phase1-container/` 单独记录。
- 外部 ORA/Genozip/SPRING/PgRC2 数字只作为调研证据，未在本地复测。
- 任何 `+` 行/CRLF/空 read 的逐字节无损声明，必须以原始字节层探针和 `cmp` 验收为准。
- `./scripts/lint.sh lint clang-debug` 在当前环境无法启动：脚本传入的
  `--exclude-header-filter` 不被 clang-tidy-18 接受；这是既有 lint 脚本/工具链问题，
  不属于本探针改动。`format-check` 与构建通过。
- NovaSeq 分箱质量与 WGS 头部快照已补齐；WGS 完整 35× run 未下载，完整 run 的序列
  冗余/覆盖分布未验证。
- 外部 ORA/Genozip/SPRING/PgRC2 工具未在本地复测；只按调研证据引用。
- Phase 1 质量 codec 实验见 `docs/max-ratio-baselines/2026-11-15-quality-codecs.md`，
  不属于本 Phase 0 change 的生产实现。
