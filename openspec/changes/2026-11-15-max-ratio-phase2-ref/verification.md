# Verification: max-ratio-phase2-ref

- Status: `Completed (experimental Phase 2 reference prototype)`
- Ready to archive: `yes`（归档动作待用户复核）
- Date: 2026-11-15
- Scope: `fqc-max-ref/v1` + 开发工具 `fqc_ref_archive`; default v2/fast CLI unchanged.

## Environment

- Repository: `open-genomics/fq-compressor`
- Branch: `master`
- Build: `clang-debug`（Clang 21.1.8、libc++、CMake/Ninja）
- htscodecs: `v1.6.7`，peeled commit `b9fc194f772e45bb0a1f44b08cbf8697a1384bae`
- 外部工具：wgsim 1.21、minimap2 2.31-r1302（仅用于生成合成数据/PAF）
- Working tree: 未提交；用户复核前不推送。

## Requirement -> Evidence Matrix

| Requirement | Scenario | Evidence | Result |
|---|---|---|---|
| Reference container | magic/header/contig table/block/footer | `tests/tools/ref_archive_test.cpp`；真实归档 `verify` | passed |
| Mapped encoding | 正链/负链、mismatch delta/alt | `RoundTripsMappedAndLiteralReads` | passed |
| Literal fallback | 无 PAF / indel CIGAR / 超 mismatch 上限 | 上述测试 + `CompressesMultipleBlocksAndHonorsMismatchLimit` | passed |
| Reference identity | 参考变化/contig 表不一致 | `RejectsReferenceDigestMismatch` | passed |
| Truncation/trailing bytes | 截断、footer 后追加字节 | `RejectsTruncatedArchive`、`RejectsTrailingBytesAfterFooter` | passed |
| Empty archive | 0 records | `SupportsEmptyArchive` | passed |
| Path safety | output == input/reference | `RejectsOutputPathAliasingInputOrReference` | passed |
| Per-block checksum | 三路原始流 + footer 滚动 checksum | 真实归档 `verify`；实现审查 | passed |
| Default v2 unchanged | full test suite | 20/20 tests，含既有 archive/e2e；`ref_archive_test` 8/8 | passed |
| Reproducible archive | 重复压缩同输入 | R1/R2 归档与重复压缩逐字节一致 | passed |

## Real-data results (synthetic mechanism validation)

| Dataset | Input bytes | Archive bytes | raw/archive | mapped/literal | verify | cmp |
|---|---:|---:|---:|---:|---|---|
| synthetic R1 | 17,423,235 | 827,688 | 21.050× | 49,194 / 806 | passed | passed |
| synthetic R2 | 17,423,235 | 827,546 | 21.054× | 49,211 / 789 | passed | passed |

输入/输出 SHA、PAF、流字节、命令与口径见
[`docs/max-ratio-baselines/2026-11-15-ref-container.md`](../../../docs/max-ratio-baselines/2026-11-15-ref-container.md)。

## Commands

| Command | Status | Summary |
|---|---|---|
| `./scripts/build.sh clang-debug` | passed | 构建 `fqc` 与 `fqc_ref_archive` |
| `./scripts/test.sh clang-debug` | passed | 20/20 tests（含 `ref_archive_test`） |
| `./scripts/test.sh clang-asan` | passed | 20/20 tests；ASan/UBSan 无报告 |
| `./scripts/lint.sh format-check` | passed | clang-format-18 |
| `./scripts/notes.sh verify` | passed | 13 notes tree/format/links |
| `git diff --check` | passed | no whitespace errors |
| `fqc_ref_archive compress/verify/decompress` | passed | R1/R2 |
| `cmp <input> <restored>` | passed | R1/R2 canonical LF FASTQ |
| repeat compress R1/R2 | passed | archive SHA 一致 |

## Notes

- 本 change **不是**最终 max-ratio：没有内置 mapper、BAM sidecar、clip/indel（Phase 2.2
  已用独立 change 实现）、参考嵌入、
  SHA-256、paired-end 联合、数字 ID token/delta、原子输出、内存上限、并发或真实 30× WGS。
- 21.05× 是 5 Mb 合成参考 + wgsim 3× + 恒定 Q23 + 纯 `<len>M` 的机制验证；参考不计入
  归档；不能外推为人类 WGS 或 ORA/Genozip 等价结果。
- 当前 `FastqParser` 边界仍是 canonical LF；`+` 行内容、CRLF/混合行尾、空 read 未保真。
- PAF 是压缩输入，不是解压依赖；解压只需同一 external reference。
