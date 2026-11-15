# fqc_ratio_probe

Phase 0 的**开发/验收专用**压缩比探针。它不安装、不进入 `fqc` 产品 CLI，也不写任何
`.fqc` 归档；用途是在写 max-ratio 格式之前，先把 FASTQ 的流级字节、质量分布和
gzip/zstd 基线测得可重复。

## 构建与运行

```bash
./scripts/build.sh clang-debug
./build/clang-debug/tools/fqc_ratio_probe -i reads.fastq
./build/clang-debug/tools/fqc_ratio_probe -i reads.fastq.gz --json
```

可选参数：

- `--zstd-level N`：zstd 基线级别，默认 19；
- `--gzip-level N`：zlib gzip 基线级别，默认 6；
- `--json`：输出 `fqc-ratio-probe/v1` JSON。

## 口径

`raw_streams` 中每个流同时给出：

- `content`：逻辑字段字节，不含行尾；
- `line_endings`：该流的 CR/LF 字节；
- `total`：二者之和。

因此：

```text
total = header.total + sequence.total + plus.total + quality.total + blank.total
```

`quality_zstd` 和 `id_zstd` 是按记录用 `\n` 归一化后的流级基线；`zstd`/`gzip` 是整个
解压后 FASTQ 字节流的基线。`gzip` 使用进程内 zlib，不是外部 `gzip` 命令，因此字节数
可能和同级别系统 gzip 文件略有差异；JSON 中标记为 `"implementation": "zlib"`。

## 与生产 parser 的边界

探针使用独立的只读 scanner，刻意保留：

- `+` 行内容；
- CRLF/LF 归属；
- 空 sequence / 空 quality；
- 最终行无换行；
- 质量字符超出可打印 ASCII 时的告警计数。

当前 `fqc::io::FastqParser` 会丢弃 `+` 行、规范化 CRLF、拒绝空 read；探针不修改它。
max-ratio 最终格式需要自己的字节保真 parser，探针只负责 Phase 0 测量。

## 测试

```bash
./scripts/test.sh -p clang-debug -f ratio_probe_test
```

测试覆盖 canonical FASTQ、CRLF、空 read、无最终换行、空行、`+` 行分类、畸形输入、
gzip 文件、JSON/文本输出和基线确定性。
