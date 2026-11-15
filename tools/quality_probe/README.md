# fqc_quality_probe

max-ratio Phase 1 的**开发/验收专用**质量流编码探针。它在不写任何归档、不改产品 CLI
的前提下，比较 htscodecs fqzcomp 与 zstd 对 quality 流的压缩体积，并验证 fqzcomp
的逐字节 round-trip。

## 依赖与固定版本

- htscodecs `v1.6.7`，tag 指向 commit `b9fc194f772e45bb0a1f44b08cbf8697a1384bae`；
- 发布 tarball：
  `https://github.com/samtools/htscodecs/archive/refs/tags/v1.6.7.tar.gz`；
- tarball SHA-256：
  `4f36d2800e97468435f724a4a0b26dfda6344b11dc16d3fa50b03f47b862e15a`；
- 许可证：BSD-3-Clause（`LICENSE.md`；`c_range_coder.h` 与 `rANS_*` 为 Public
  Domain/CC0，详见上游文件头）。

本探针只直接复用 `fqzcomp_qual.c` / `utils.c` 及其头文件，不把 htscodecs 纳入产品
依赖或生产构建。探针调用 `fqz_compress(vers=4, ...)`（CRAM 4.0 包装；模型内部版本为
`FQZ_VERS 5`）。

## 构建

```bash
curl -L -o /tmp/htscodecs-v1.6.7.tar.gz \
  https://github.com/samtools/htscodecs/archive/refs/tags/v1.6.7.tar.gz
sha256sum /tmp/htscodecs-v1.6.7.tar.gz
# 4f36d2800e97468435f724a4a0b26dfda6344b11dc16d3fa50b03f47b862e15a

mkdir -p /tmp/htscodecs-v1.6.7
tar -xzf /tmp/htscodecs-v1.6.7.tar.gz \
  -C /tmp/htscodecs-v1.6.7 --strip-components=1

./tools/quality_probe/build.sh /tmp/htscodecs-v1.6.7
```

构建输出在 `build/quality-probe/fqc_quality_probe`。脚本从 Conan 缓存读取
`zstd/1.5.7` 的头文件与静态库；先运行 `conan install .`。

## 用法

```bash
build/quality-probe/fqc_quality_probe reads.fastq --block-records 25000
build/quality-probe/fqc_quality_probe reads.fastq --block-records 0 --json
```

- `--block-records N`：每 N 条 read 组成一个独立块；`0` 表示整文件一个块；
- `--strategy 0..3`：固定 fqzcomp 策略；省略时每块尝试 0 与 1，选较小者；
- `--zstd-level N`：zstd 对照基线级别，默认 19；
- `--json`：输出 `fqc-quality-probe/v1` JSON。

## 口径与边界

- 输入必须是 4 行结构的普通 FASTQ；支持 LF/CRLF、空 read 与 literal `+` 行；
  quality 必须是可打印 Phred+33（33..126），否则 fail closed。
- fqzcomp 输入是 **quality ASCII 减 33 后的二进制值**，不含换行；记录长度与
  `FQZ_FREAD2`/reverse flags 由 `fqz_slice` 提供。本探针目前统一 flags=0，不模拟
  paired-end read2。
- zstd 基线只压缩 quality 值；与 `fqc_ratio_probe` 的 `quality_zstd`（带记录换行）
  相差很小，但不是同一字节流，比较时必须注明。
- fqzcomp 每块独立训练模型；小块会因模型 warmup 变差。max-ratio 块大小应至少
  数百万 base，而不是按固定 read 条数机械切块。
- 探针不安装、不进入 `fqc` 主 CLI，也不改变 `fqc-sequential/v2`。

## 已记录实验

- 真实 HiSeq 25k reads / 2.525M bases：fqzcomp strategy 1 558,189 B，zstd19
  678,308 B（−17.7%）；
- NovaSeq DRR174812 头部 303,610 reads / 45.5415M bases：fqzcomp strategy 0
  2,159,788 B，zstd19 2,802,326 B（−22.9%）；
- WGS SRR14724532 头部 298,974 reads / 45.1451M bases：fqzcomp 自适应 0/1
  2,168,649 B，zstd19 2,823,235 B（−23.2%）。

完整命令、输入 SHA、分块曲线与归档影响估算见
[`docs/max-ratio-baselines/2026-11-15-quality-codecs.md`](../../docs/max-ratio-baselines/2026-11-15-quality-codecs.md)。
