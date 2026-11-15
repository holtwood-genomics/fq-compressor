# Verification: max-ratio-paired-end

- Status: `Completed (real WGS R1/R2 head validation)`
- Date: 2026-11-15
- Scope: `SRR14724532` R1/R2 各 298,974 reads + 完整 UCSC hg38；不是完整 30× run。

## Requirement -> Evidence Matrix

| Requirement | Evidence | Result |
|---|---|---|
| Paired input | R1/R2 各 298,974 reads，ID 全对齐 | passed |
| R2 PAF | 48,788,417 B；296,346 primary alignments；SHA 固定 | passed |
| R2 single archive | 5,130,877 B；19.477× raw | passed |
| Two single sum | 9,678,812 B；20.650× raw | passed |
| Paired archive | 8,825,785 B；22.646× raw；−853,027 B（−8.813%） | passed |
| Mate delta | 277,453 / 298,974（92.80%） | passed |
| Alignment stream | 4,939,473 → 4,094,927 B（−17.10%） | passed |
| Round-trip | `verify` + `decompress-paired` + R1/R2 `cmp` | passed |
| Determinism | 两次 paired 压缩 SHA 相同 | passed |
| Legacy | 旧 R1 单端归档仍可 `verify`；新代码重压与旧归档逐字节一致 | passed |
| External baselines | gzip/zstd/FQC/CRAM 已固定 | passed |
| Full 30× | 未下载完整 run；未做 ORA/Genozip | pending |

## Quality Gates

| Command | Result |
|---|---|
| `./scripts/test.sh clang-debug` | 22/22 passed |
| `./scripts/test.sh clang-asan` | 22/22 passed |
| `./scripts/lint.sh format-check` | passed |
| `git diff --check` | passed |
| `ref_archive_test` | 14/14 cases passed |

## Notes

- 主口径是 external reference + external PAF，不计入归档；
- 计入 PAF 为 1.874×，计入完整参考为 0.0609×；
- 旧单端字节兼容：新代码重压 R1 头部得到 4,547,935 B，SHA
  `6f50a3146e018c33fffe6be3a21af9163b8ae1223d1b386f6f440507bb3b647a`，与旧归档逐字节一致；
- 22.646× 是 R1/R2 头部小切片，不是完整 30×，也不是业界第一；
- CRAM 是两个独立 alignment container 的大小之和，不是 paired CRAM，也不是 FASTQ
  归档等价物；
- 详细命令、SHA 和字节账见
  [2026-11-15-real-wgs-paired.md](../../../docs/max-ratio-baselines/2026-11-15-real-wgs-paired.md)。
