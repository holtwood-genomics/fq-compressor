# max-ratio 数据集 manifest

- 状态：Phase 0 可追溯数据集清单（2026-11-15）
- 机器可读副本：[datasets.json](datasets.json)
- 设计：[docs/max-ratio-design.md](../max-ratio-design.md)
- 探针：[tools/ratio_probe/README.md](../../tools/ratio_probe/README.md)

本文件只固定**数据集身份**与本地快照，不代表 max-ratio 已实现。任何压缩比数字必须同时写明
数据集、快照范围、工具版本和口径；完整 run 与头部子集不能混为一谈。

## 1. 清单

| ID | 角色 | 来源/accession | 平台/类型 | 完整 run 规模 | 本地快照 | 状态 |
|---|---|---|---|---|---|---|
| `hiseq-hartwig-25k-r1` | 全质量 HiSeq 质量/ID 基线 | Hartwig Medical `100k_reads_hiseq` R1 | HiSeq，短读 | 25,000 reads / 2,525,000 bases | 6,538,676 B FASTQ；1,957,197 B gzip | 已下载并固定 SHA |
| `novaseq-drr174812-r1-head` | NovaSeq 分箱质量基线 | ENA `DRR174812` R1；study `PRJDB8092` | NovaSeq 6000，RNA-Seq，2×150 | 14,723,420 spots / 4,417,026,000 bases | gzip 前 21,110,784 B；解压并裁成 303,610 reads / 45,541,500 bases | 已下载头部子集并固定 SHA |
| `wgs-srr14724532-r1-head` | 深覆盖 WGS 质量基线 | ENA `SRR14724532` R1；GIAB/NIST | NovaSeq 6000，WGS，2×150，约 35× | 355,791,257 spots / 107,448,959,614 bases（双端合计） | gzip 前 21,110,784 B；解压并裁成 298,974 reads / 45,145,074 bases | 头部子集已下载并固定 SHA；完整 run 未下载 |
| `synthetic-ref-5m-wgsim` | Phase 2 参考机制验证 | 本地固定 seed 20261115 | 随机 5 Mb 参考，wgsim 3× 双端 150 bp | — | 参考 5,071,439 B；R1/R2 各 17,423,235 B | 合成输入与 PAF 已固定 SHA |
| `synthetic-ref-5m-wgsim-structured-id` | Phase 2.1 ID codec 验证（派生） | 从上一行 R1 派生 | 同左，ID/PAF query name 改为结构化 | — | R1 17,700,000 B；PAF 9,071,979 B；归档 331,553 B | 派生输入/归档 SHA 已固定 |

## 2. HiSeq 小样本

- 来源 URL：
  `https://raw.githubusercontent.com/hartwigmedical/testdata/master/100k_reads_hiseq/TESTX/TESTX_H7YRLADXX_S1_L001_R1_001.fastq.gz`
- 本地文件：
  - `hiseq_R1.fastq`：6,538,676 B，
    SHA-256 `de63a088f1a748153e0a42814dbb4e7dd8fb95a01fb2b2bafd51a2e975075058`
  - `hiseq_R1.fastq.gz`：1,957,197 B，
    SHA-256 `5c90a41c0fb951013fb7dc876ccbbb4a4dc9ed0526476f3d7c2086f0b9b18198`
- 统计：25,000 reads，101 bp，2,525,000 bases，12,391 N，38 个质量符号，平均 Q37.26。
- 用途：质量/ID/metadata 与当前 FQC 归档基线；**不代表深覆盖 WGS 的跨 read 冗余**。
- 质量特征：全质量范围（非分箱），适合检验位置/上下文模型。

## 3. NovaSeq 分箱质量头部子集

完整 run（ENA 元数据，2026-11-15 查询）：

| 字段 | 值 |
|---|---|
| run | `DRR174812` |
| study | `PRJDB8092` |
| sample | `SAMD00164453` |
| instrument | Illumina NovaSeq 6000 |
| library | RNA-Seq / TRANSCRIPTOMIC / PolyA |
| layout | PAIRED，2×150 |
| spots | 14,723,420 |
| bases | 4,417,026,000 |
| R1 gzip | 1,026,013,177 B，MD5 `a75001a9d90bfaf18a62d22b74a3514c` |
| R2 gzip | 1,091,492,474 B，MD5 `764dd3d54294533f0bfbb545185ed27b` |

本地快照只取 R1 gzip 的前 21,110,784 字节，并裁掉末尾不完整记录：

| 文件 | 字节 | SHA-256 |
|---|---:|---|
| `DRR174812_1.head21m.fastq.gz` | 21,110,784 | `23b6941a21236aac60f5e170e9d32ad80d18e5b9642a314c5588711215f38b04` |
| `DRR174812_R1.head.fastq` | 100,272,700 | `c8c02eccca86ee2a67f7ef84a0d36706f25ec9426dc7b1e531b5686bafb5b40e` |

- 子集统计：303,610 reads，150 bp，45,541,500 bases，1,259 N，4 个质量符号
  （`F` 42,317,312；`:` 2,020,381；`,` 1,202,548；`#` 1,259），平均 Q35.78。
- 复现方式：HTTP Range `0-21110783` 取 gzip 头部，再用 zlib/gzip 解压，按 4 行结构
  保留完整记录；末尾不完整记录必须丢弃，不能静默补字节。
- **口径警告**：这是 RNA-Seq 的分箱质量样本，不是 WGS。它用于验证“分箱质量下质量
  codec 的行为”，不能用于声称 WGS 序列冗余或 18–20× 总归档比。
- 质量特征：四符号、极高分箱（92.9% 为 `F`），是 zstd 与上下文模型的压力面。

## 4. 深覆盖 WGS 头部子集与完整 run manifest

完整 run（ENA 元数据，2026-11-15 查询）：

| 字段 | 值 |
|---|---|
| run | `SRR14724532` |
| study | `PRJNA734598`（NIST Genome in a Bottle） |
| instrument | Illumina NovaSeq 6000 |
| library | WGS / GENOMIC / RANDOM |
| layout | PAIRED，2×150，约 35× |
| spots | 355,791,257 |
| bases | 107,448,959,614（双端合计） |
| R1 gzip | 25,019,246,552 B，MD5 `40eb3a20e288a581b23197fb32a5a5cc` |
| R2 gzip | 25,979,206,586 B，MD5 `9846d10d1eca7c52948cbe5f94d2cdaf` |

本地快照取 R1 gzip 的前 21,110,784 字节，并裁掉末尾不完整记录：

| 文件 | 字节 | SHA-256 |
|---|---:|---|
| `SRR14724532_1.head21m.fastq.gz` | 21,110,784 | `286836d9d79ad8470ddcd2b395562aa4f25a347e8b424a9bb1ad5c159378b95b` |
| `SRR14724532_R1.head.fastq` | 99,934,080 | `d4f6c8bee191ee8cc5207cca5fb86659a2aaa02e3f493b35f2ed50027ab6e0f4` |

- 子集统计：298,974 reads，150 bp，45,145,074 bases，831 N，4 个质量符号，平均 Q35.68。
- 当前 FQC 基线（64 MiB / parse-workers 4 / quality-level 1）：16,811,413 B，
  SHA-256 `e01c96e6b34f63822d0857cc8ab7ff4a83c35ac8ee1bfa7d91dca511ebc3ee6e`。
- 质量 codec：fqzcomp 2,168,649 B vs zstd19 2,823,235 B（−23.19%）；当前 quality
  zstd1 4,779,205 B。按只替换质量流估算，总归档可缩 15.53%（估算，不是已实现）。
- 状态：**头部子集已下载并固定 SHA；完整 35× run 未下载**。子集是 WGS、分箱质量，
  但只覆盖文件头部约 300k reads，不能代表完整 run 的序列冗余/覆盖分布。
- 用途：Phase 1 质量 codec 的 WGS 质量 regime 验证；Phase 2/3 的完整序列冗余验证仍需
  完整 run 或固定分片快照。
- 不能把 ENA 的 spot/base count 直接当成本地 FASTQ 字节；本地快照必须另记 SHA-256。

## 5. Phase 2 合成参考机制验证

这不是公共数据集，也不用于声称人类 WGS 压缩比；它只固定 Phase 2 `fqc-max-ref/v1`
的机制验证输入。参考和 reads 均由固定 seed 生成，命令在 2026-11-15 实际执行。

参考生成：

```bash
python3 - <<'PY'
import random
from pathlib import Path
rng = random.Random(20261115)
seq = ''.join(rng.choice('ACGT') for _ in range(5_000_000))
out = Path('/tmp/fqc-ref-bench-20261115/ref5m.fa')
with out.open('w') as f:
    f.write('>chrSyn5M\n')
    for i in range(0, len(seq), 70):
        f.write(seq[i:i+70] + '\n')
PY
```

reads/PAF：

```bash
wgsim -N 50000 -1 150 -2 150 -S 20261115 \
  -e 0.005 -d 500 -s 50 -r 0.001 -R 0.15 -X 0.3 \
  ref5m.fa r1.fq r2.fq
minimap2 -x sr -c -t 4 ref5m.fa r1.fq > r1.paf
minimap2 -x sr -c -t 4 ref5m.fa r2.fq > r2.paf
```

| 文件 | 字节 | SHA-256 |
|---|---:|---|
| `ref5m.fa` | 5,071,439 | `df2ce24106ef8ab086e74c001a8b01570403e8f2c7d29831ba9a4c63a1ff543b` |
| `r1.fq` | 17,423,235 | `ca35dbdf9dc7886eac5f05bec1184f1ab2cedffcb9959109eb524eeaf6f572bb` |
| `r2.fq` | 17,423,235 | `c3dcae4be810487baf319764a9e0abfe588b1fbf920d13d8b2e435c3d38808ca` |
| `r1.paf` | 9,445,200 | `b50e56723d1994862d23e2de521a27cedef7092c4125c5e9dcfcd84e8f8c7a74` |
| `r2.paf` | 9,446,372 | `8851a952e70e2de34fcc992b886b5e46b2adf71329c772bd5de0b0f732b987d1` |

- 50,000 对 × 2 × 150 bp = 15,000,000 bases，对 5 Mb 参考约 3× 覆盖；
- wgsim 1.21，minimap2 2.31-r1302；质量单符号 `8`（Q23），不是真实质量分布；
- 结果是参考/PAF/编码/校验链路的机制验证，不是 30× 人类 WGS 结论；
- 完整归档、命令和边界见
  [2026-11-15-ref-container.md](2026-11-15-ref-container.md)。

### 5.1 派生结构化 ID 变体（Phase 2.1）

不新增公共数据集；从上面的合成 R1 派生一个只改变 ID/PAF query name 的变体，用于验证
自适应 ID front coding：

| 文件 | 字节 | SHA-256 |
|---|---:|---|
| `r1.struct.fq` | 17,700,000 | `f53826400a4b69daaf8072a33d01fb2ecd37f3297599b2997628f0fbeee8baaa` |
| `r1.struct.paf` | 9,071,979 | `51ca888a30de942c792ffd5772fb0a4d59ea9e1339a47e8741dd6aca42488da7` |

- 序列、参考、PAF 坐标、质量值不变；只把 ID 改成 Illumina 风格并同步改写 PAF 第一列。
- Phase 2.1 归档 331,553 B（53.385× raw，`front_id_blocks=2`），`verify` 与 `cmp` 通过；
  Phase 2.2 CIGAR 后为 296,082 B（59.781× raw），`verify` 与 `cmp` 仍通过。
- 这是 ID + CIGAR codec 机制验证，不是真实 WGS；完整命令与边界见
  [2026-11-15-id-codec.md](2026-11-15-id-codec.md) 与
  [2026-11-15-ref-cigar.md](2026-11-15-ref-cigar.md)。

## 6. ENA 元数据查询

可复现查询：

```bash
curl -s 'https://www.ebi.ac.uk/ena/portal/api/filereport?accession=DRR174812&result=read_run&fields=run_accession,study_accession,sample_accession,instrument_model,library_strategy,library_layout,read_count,base_count,fastq_ftp,fastq_bytes,fastq_md5&format=tsv'

curl -s 'https://www.ebi.ac.uk/ena/portal/api/filereport?accession=SRR14724532&result=read_run&fields=run_accession,study_accession,sample_accession,instrument_model,library_strategy,library_layout,read_count,base_count,fastq_ftp,fastq_bytes,fastq_md5&format=tsv'
```

ENA 页面与文件可能更新；本清单以 2026-11-15 查询结果和本地 SHA 为证据。若 accession
内容变化，必须新建快照并更新本文件，不得覆盖旧 SHA。

## 7. 未验证边界

- `hiseq-hartwig-25k-r1` 只有 25k reads，不能代表 30× WGS。
- `novaseq-drr174812-r1-head` 是 RNA-Seq 头部子集，不是完整 run，也不是 WGS。
- `wgs-srr14724532-r1-head` 只有头部约 300k reads；完整 run 未下载，不能代表 35× WGS。
- 三个数据集都没有 paired-end 联合压缩、CRLF、IUPAC 或空 read 的完整验收；这些由
  `ratio_probe_test` 的合成 fixture 单独覆盖。
- 外部 ORA/Genozip/SPRING/PgRC2 数字仍只按调研证据引用，未在本地复测。
