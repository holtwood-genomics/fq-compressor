# fq-compressor (C++) - Project Context

## Identity

- **Canonical repository**: `open-genomics/fq-compressor`
- **Product name**: `fqc`
- **Archive extension**: `.fqc`
- **Format family**: `fqc-sequential/v2`
- **Binary**: `fqc`
- **Lifecycle**: active development
- **Language**: C++23 (CMake + Conan)

## Core contracts

| Capability | Path | Description |
|---|---|---|
| `archive-format` | `openspec/specs/archive-format/` | Sequential v2 binary layout, header/frame/footer, checksums |
| `format-governance` | `openspec/specs/format-governance/` | Same-name `.fqc` format family coexistence documentation contract |
| `ci-quality` | `openspec/specs/ci-quality/` | CI quality gates: sanitizers, TSan, fuzz, release build, static analysis, notes |

## Research tracks

| Track | Evidence | Status |
|---|---|---|
| `max-ratio-research` | [docs/max-ratio-design.md](../docs/max-ratio-design.md), [datasets.md](../docs/max-ratio-baselines/datasets.md), [quality codecs](../docs/max-ratio-baselines/2026-11-15-quality-codecs.md), [max container](../docs/max-ratio-baselines/2026-11-15-max-container.md), [reference prototype](../docs/max-ratio-baselines/2026-11-15-ref-container.md), [ID codec](../docs/max-ratio-baselines/2026-11-15-id-codec.md), [CIGAR baseline](../docs/max-ratio-baselines/2026-11-15-ref-cigar.md), [Phase 1 container contract](changes/2026-11-15-max-ratio-phase1-container/design.md), [Phase 2 reference contract](changes/2026-11-15-max-ratio-phase2-ref/design.md), [Phase 2.1 ID codec contract](changes/2026-11-15-max-ratio-phase2-id-codec/design.md), [Phase 2.2 CIGAR contract](changes/2026-11-15-max-ratio-phase2-cigar/design.md), [tools/ratio_probe/README.md](../tools/ratio_probe/README.md), [tools/quality_probe/README.md](../tools/quality_probe/README.md), [tools/ref_archive/README.md](../tools/ref_archive/README.md) | Phase 0 probe/manifests done; Phase 1 quality gate and experimental container done; Phase 2 PAF reference prototype, adaptive ID codec and clip/indel CIGAR done on synthetic data; real 30x WGS and Phase 3 pending |

## External boundaries

- **`fq-compressor-rust`**: shares product name `fqc` and extension `.fqc` but uses a
  different format family (`fqc-indexed/v2`). Each reader must reject the other's magic.
- **Decision `FQC-DEC-001`**: both implementations keep `fqc` / `.fqc`; format family
  distinguished by archive magic, not suffix.

## Validation commands

```bash
./scripts/lint.sh format-check
./scripts/build.sh clang-debug
./scripts/test.sh -p clang-debug
```

## Active changes

| Date | Change | Status |
|---|---|---|
| 2026-11-14 | `max-ratio-phase0-benchmark` | Phase 0 measurement complete; ready for review; no product format/CLI change |
| 2026-11-15 | `max-ratio-quality-codec` | Quality codec probe complete; no product format/CLI change |
| 2026-11-15 | `max-ratio-phase1-container` | Experimental `fqc-max/v1` container and CLI implemented; canonical-LF boundary |
| 2026-11-15 | `max-ratio-phase2-ref` | Experimental `fqc-max-ref/v1` + `fqc_ref_archive`; PAF external reference prototype; synthetic evidence only |
| 2026-11-15 | `max-ratio-phase2-id-codec` | Adaptive raw/front-coded ID codec in `fqc-max-ref/v1`; synthetic and header-only evidence |
| 2026-11-15 | `max-ratio-phase2-cigar` | M/=/X/I/D + query clip extension in `fqc-max-ref/v1`; synthetic evidence only |

## Archived changes

Completed, merged, and moved under `openspec/changes/archive/`:

| Date | Change | Capability |
|---|---|---|
| 2026-08-19 | `freeze-sequential-v2-format` | `archive-format` |
| 2026-08-19 | `correct-verify-contract` | `archive-format` |
| 2026-08-19 | `recognize-indexed-fqc-family` | `archive-format` |
| 2026-08-19 | `add-ci-sanitizer-gate` | `ci-quality` |
| 2026-08-20 | `document-fqc-format-family` | `format-governance` |

## Authority rules

- `src/` is the implementation source of truth.
- Models must not commit, push, create PRs, or publish without explicit authorization.
- High-risk changes (format, compatibility) use lightweight OpenSpec changes.
