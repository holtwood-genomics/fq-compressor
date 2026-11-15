# Verification: max-ratio-real-wgs-validation

- Status: `Completed (real WGS R1 head validation)`
- Date: 2026-11-15
- Scope: `SRR14724532` R1 头部 298,974 reads + 完整 UCSC hg38；不是完整 30× run。

## Requirement -> Evidence Matrix

| Requirement | Evidence | Result |
|---|---|---|
| Real WGS reads | 298,974 reads / 45,145,074 bases / 99,934,080 B | passed |
| Full reference | hg38 3,273,481,150 B，SHA 已固定 | passed |
| Minimap2 PAF | 298,179 primary alignments，49,043,976 B，SHA 已固定 | passed |
| Archive | 4,547,935 B；21.974× raw；mapped 296,343 / literal 2,631 | passed |
| Round-trip | `verify` + `decompress` + `cmp`；恢复 FASTQ SHA 与输入一致 | passed |
| Determinism | 两次压缩归档 SHA 一致 | passed |
| Soft-mask fix | `NormalizesSoftMaskedReferenceCase` | passed |
| Three-stream account | ID 3,701 / quality 2,181,008 / alignment 2,352,120 B | passed |
| External baselines | gzip 21,097,162 / zstd19 16,463,409 / FQC 16,811,413 / CRAM 5,929,273 B | passed |
| Full 30× | 未下载完整 run；未做 paired-end | pending |

## Commands

```bash
./scripts/fetch_real_wgs_ref_bench.sh
```

脚本会校验固定 SHA，构建 hg38 minimap2 index，生成 PAF，运行压缩/verify/decompress/cmp，
并生成 gzip/zstd/FQC/CRAM 对照。完整输入/归档 SHA、命令和边界见
[2026-11-15-real-wgs-srr14724532.md](../../../docs/max-ratio-baselines/2026-11-15-real-wgs-srr14724532.md)。

## Quality Gates

| Command | Result |
|---|---|
| `./scripts/test.sh clang-debug` | 22/22 passed |
| `./scripts/test.sh clang-asan` | 22/22 passed；ASan/UBSan 无报告 |
| `./scripts/lint.sh format-check` | clang-format-18 passed |
| `./scripts/notes.sh verify` | 33 notes passed；0 archived |
| `shellcheck scripts/fetch_real_wgs_ref_bench.sh` | passed |
| `git diff --check` | passed |
| `python3 -m json.tool docs/max-ratio-baselines/datasets.json` | passed |

## Notes

- 主口径是 external reference + external PAF，不计入归档；
- 计入 PAF 为 1.865×，计入完整参考为 0.0305×；
- 这是单 R1 头部，不是完整 30×、paired-end 或外部工具同数据结论；
- CRAM 是 alignment container，不是 FASTQ 字节归档。
