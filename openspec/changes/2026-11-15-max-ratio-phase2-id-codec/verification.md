# Verification: max-ratio-phase2-id-codec

- Status: `Completed (experimental Phase 2.1 ID codec extension)`
- Ready to archive: `yes`（归档动作待用户复核）
- Date: 2026-11-15
- Scope: `fqc-max-ref/v1` `idCodec=2`; default v2/fast CLI unchanged.

## Requirement -> Evidence Matrix

| Requirement | Scenario | Evidence | Result |
|---|---|---|---|
| Adaptive ID codec | structured vs random IDs | `UsesFrontCodingForStructuredIds`; wgsim SHA regression | passed |
| Front-coded decode | prefix/suffix/length boundaries | codec implementation + round-trip tests | passed |
| Backward compatibility | old `idCodec=1` archives | `verify` on pre-change R1/R2 archives | passed |
| Byte-identical raw fallback | random wgsim IDs | `cmp r1.fqmr r1.idcodec.fqmr` | passed |
| Checksum invariant | checksum over raw ID stream | implementation review + `verify` | passed |
| Structured-ID archive | 17,700,000 B input | 331,553 B; 53.385× raw; `front_id_blocks=2` | passed |
| Round-trip | structured synthetic R1 | `verify` + decompress `cmp` | passed |
| Default v2 unchanged | full suite | 20/20 CTest; `ref_archive_test` 8/8 | passed |

## Key Measurements

| Item | Value |
|---|---:|
| Real WGS head ID zstd19 | 114,099 B |
| Real WGS head front-coded zstd19 | 1,085 B |
| Real NovaSeq head ID zstd19 | 111,762 B |
| Real NovaSeq head front-coded zstd19 | 1,105 B |
| Random wgsim ID raw zstd19 | 495,131 B |
| Random wgsim ID front zstd19 | 545,494 B |
| Structured synthetic archive | 331,553 B |
| Structured synthetic raw/archive | 53.385× |
| Original wgsim archive | 827,688 B (unchanged) |

输入/PAF/归档 SHA、命令和边界见
[`docs/max-ratio-baselines/2026-11-15-id-codec.md`](../../../docs/max-ratio-baselines/2026-11-15-id-codec.md)。

## Commands

| Command | Status | Summary |
|---|---|---|
| `./scripts/build.sh clang-debug` | passed | 构建 `fqc_ref_archive` |
| `ctest --test-dir build/clang-debug -R ref_archive_test` | passed | 8/8 cases |
| `./scripts/test.sh clang-debug` | passed | 20/20 CTest |
| `./scripts/test.sh clang-asan` | passed | 20/20 CTest；ASan/UBSan 无报告 |
| `./scripts/lint.sh format-check` | passed | clang-format-18 |
| `./scripts/notes.sh verify` | passed | notes tree/format/links |
| `git diff --check` | passed | no whitespace errors |
| `fqc_ref_archive compress/verify/decompress` | passed | structured ID R1 |
| `cmp` | passed | structured ID restored FASTQ |
| wgsim raw fallback `cmp` | passed | archive byte-identical |

## Notes

- 53.385× 是人工结构化 ID 的合成机制验证，不是人类 30× WGS 结果；参考不计入、质量恒定
  Q23、只接受纯 `<len>M` 比对。
- 真实 ID 探针只测 header 流，不代表完整归档压缩比。
- 没有数字 token/delta、跨块 ID 模型、paired-end 联合、正式格式冻结或真实 WGS 验收。
