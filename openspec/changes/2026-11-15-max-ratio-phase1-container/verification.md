# Verification: max-ratio-phase1-container

- Status: `Completed (experimental container)`
- Ready to archive: `yes`（归档动作待用户复核）
- Date: 2026-11-15
- Scope: `fqc-max/v1` Phase 1 container and experimental CLI; default v2 behavior unchanged.

## Environment

- Repository: `holtwood-genomics/fq-compressor`
- Branch: `master`
- Build: `clang-debug`（Clang 21.1.8、libc++、CMake/Ninja）
- htscodecs: `v1.6.7`，peeled commit `b9fc194f772e45bb0a1f44b08cbf8697a1384bae`，
  tarball SHA-256 `4f36d2800e97468435f724a4a0b26dfda6344b11dc16d3fa50b03f47b862e15a`
- Working tree: 未提交；用户复核前不推送。

## Requirement -> Evidence Matrix

| Requirement | Scenario | Evidence | Result |
|---|---|---|---|
| Independent container | magic/header/footer | `tests/format/max_archive_test.cpp` 检查 `FQCMX1\r\n`；未知 magic 被拒 | passed |
| Cross-block round-trip | 2 blocks, mixed records | `MaxArchiveTest.RoundTripsRecordsAcrossBlocks` | passed |
| Empty archive | header + footer only | `MaxArchiveTest.SupportsEmptyArchive`、e2e 空文件 | passed |
| Corrupt/truncated input fails closed | truncation / payload corruption | `RejectsTruncatedArchive`、`RejectsCorruptBlockPayload` | passed |
| Quality codec selection | fqzcomp vs zstd per block | 块头 codec/strategy；三个真实归档 fqzcomp 块占多数 | passed |
| Full decode/verify | 三个真实归档 | `max-verify` 全部通过 | passed |
| Byte-exact output | canonical LF FASTQ | `max-decompress` + `cmp` 三个归档 | passed |
| Default v2 unchanged | full test suite | 19/19 tests（当时；Phase 2 后当前全量 20/20），含既有 archive/e2e | passed |
| Reproducible archive | repeat `max-compress` | HiSeq SHA 一致；三种输入均复核 | passed |

## Real-data results

| Dataset | Input bytes | v2 bytes | max bytes | Reduction vs v2 | max-verify | cmp |
|---|---:|---:|---:|---:|---|---|
| HiSeq 25k R1 | 6,538,676 | 1,634,437 | 1,285,285 | −21.36% | passed | passed |
| NovaSeq RNA-Seq head | 100,272,700 | 16,344,935 | 12,280,644 | −24.87% | passed | passed |
| WGS head | 99,934,080 | 16,811,413 | 12,828,169 | −23.69% | passed | passed |

输入/输出 SHA-256、命令与口径见
[`docs/max-ratio-baselines/2026-11-15-max-container.md`](../../../docs/max-ratio-baselines/2026-11-15-max-container.md)。

## Commands

| Command | Status | Summary |
|---|---|---|
| `./scripts/build.sh clang-debug` | passed | 构建 `build/clang-debug/src/fqc` |
| `./scripts/test.sh clang-debug` | passed | 19/19 tests（当时；含 max_archive_test、e2e） |
| `./scripts/lint.sh format-check` | passed | clang-format-18 |
| `./scripts/notes.sh verify` | passed | notes tree/format/links |
| `git diff --check` | passed | no whitespace errors |
| `fqc max-verify <three archives>` | passed | 完整解码，无错误 |
| `cmp -s <input> <max-decompress output>` | passed | 三个样本逐字节一致 |

## Notes

- 本 change **不是**最终 max-ratio：没有参考基因组/BAM sidecar、伪基因组、read 重排、
  `--max-ratio` 顶层开关或 paired-end 联合压缩。
- 当前 `FastqParser` 边界：CRLF 规范化、`+` 行内容丢弃、空 sequence 拒绝；因此只对
  canonical LF 输入承诺逐字节 round-trip。
- 相对 v2 的缩减包含 ID/sequence 的 zstd19 收益，不能全部归因于 fqzcomp；质量流独立
  对照见 `docs/max-ratio-baselines/2026-11-15-quality-codecs.md`。
- NovaSeq 是 RNA-Seq 头部子集；WGS 是约 300k reads 头部子集，完整 35× run 未下载。
- `--zstd-level 19` 的压缩时间明显高于 fast 路径；本轮不设速度/内存门禁。
- 旧 change `2026-11-15-max-ratio-quality-codec` 的“生产 src 未改”只描述当时探针范围；
  容器实现由本 change 单独记录。
