# Verification: max-ratio-phase2-cigar

- Status: `Completed (experimental Phase 2.2 CIGAR extension)`
- Ready to archive: `yes`（归档动作待用户复核）
- Date: 2026-11-15
- Scope: `fqc-max-ref/v1` mapped read 的 clip/indel CIGAR；默认 v2/fast CLI 不变。

## Requirement -> Evidence Matrix

| Requirement | Scenario | Evidence | Result |
|---|---|---|---|
| CIGAR parse | M/=/X/I/D | implementation + `RoundTripsClippedAndIndelReads` | passed |
| Unsupported CIGAR | S/H/N/P/空/超限 | fallback literal path; existing fixture | passed |
| Query/target span | PAF 坐标与操作不符 | encode returns literal; decode rejects | passed |
| Extend flags | only clipped/indel mapped | pure M/legacy archives still verify | passed |
| Positive clip | left/right query clip | test round-trip | passed |
| Positive I/D | insertion/deletion | test round-trip | passed |
| Negative CIGAR | reverse-complement aligned query | test round-trip | passed |
| Legacy archive | Phase 2 R1 | `fqc_ref_archive verify` | passed |
| R1 archive | 17,423,235 B input | 792,217 B; 21.993×; mapped 49,998 / literal 2 | passed |
| R2 archive | 17,423,235 B input | 796,633 B; 21.871×; mapped 50,000 / literal 0 | passed |
| Round-trip | R1/R2 | decompress + `cmp` | passed |
| Reproducibility | R1 repeat | archive byte-identical | passed |
| Structured ID | R1 struct | 296,082 B; 59.781×; `front_id_blocks=2` | passed |
| Default v2 unchanged | full suite | 22/22 CTest | passed |

## Key Measurements

| Item | Value |
|---|---:|
| R1 Phase 2 archive | 827,688 B |
| R1 Phase 2.2 archive | 792,217 B |
| R1 raw/archive | 21.993× |
| R2 Phase 2 archive | 827,546 B |
| R2 Phase 2.2 archive | 796,633 B |
| R2 raw/archive | 21.871× |
| R1 alignment stream | 330,759 → 295,288 B |
| R2 alignment stream | 330,199 → 299,286 B |
| R1 mapped/literal | 49,194/806 → 49,998/2 |
| R2 mapped/literal | 49,211/789 → 50,000/0 |
| Structured ID archive | 331,553 → 296,082 B |

输入/归档 SHA、命令和边界见
[`docs/max-ratio-baselines/2026-11-15-ref-cigar.md`](../../../docs/max-ratio-baselines/2026-11-15-ref-cigar.md)。

## Commands

| Command | Status | Summary |
|---|---|---|
| `./scripts/build.sh clang-debug` | passed | 构建 `fqc_ref_archive` |
| `ctest --test-dir build/clang-debug -R ref_archive_test` | passed | 9/9 cases |
| `./scripts/test.sh clang-debug` | passed | 22/22 CTest |
| `./scripts/test.sh clang-asan` | passed | 22/22 CTest；ASan/UBSan 无报告 |
| `./scripts/lint.sh format-check` | passed | clang-format-18 |
| `./scripts/notes.sh verify` | passed | notes tree/format/links |
| `git diff --check` | passed | no whitespace errors |
| R1/R2 compress/verify/decompress | passed | new CIGAR archives |
| `cmp` | passed | restored FASTQ |
| legacy Phase 2 verify | passed | 827,688 / 331,553 B archives |

## Notes

- 21.99× / 21.87× 是 5 Mb 合成参考 + wgsim 3× + 恒定 Q23 的机制验证，不是人类 30× WGS；
  参考不计入、PAF 不计入。
- R1 仍有 2 条 literal，原因是 PAF 中没有这两条 read 的 primary 比对。
- 没有 S/H/N/P、长读 spliced alignment、BAM/CRAM、内置 mapper、参考嵌入、SHA-256、
  paired-end 联合、正式格式冻结或速度门禁。
