# fq-compressor 并发流水线开发历程

业余练手项目，目标：练手 C++ 并发流水线编程，追求轻灵巧、易维护，不引重框架。
本文串起从架构修复到阶段 H 收束的开发脉络，详细阶段计划见 `docs/roadmap.md`，问题复盘见
`docs/postmortems/`，真实语料验收见 `docs/real-corpus.md`。

## 定位与约束

- **业余练手**：练并发流水线，不是产品。排除产品 closeout 流程与重框架。
- **不引重框架**：TBB / 线程池 / 异步 I/O（io_uring）/ lock-free MPMC / 过早泛型 pipeline 框架，一概不做。
- **一次一个并发知识点**：同步原语、取消、多级、并行分阶段练，不混。
- **每步可验证可回退**：`clang-tsan` 验无竞争，`test_performance.sh` 量化，每步独立 commit。

## 起点：架构修复（2026-07-24 评审后）

架构评审发现 5 项问题并全部修复（commit 8e3dd95、3739f9b）：README 死链与产品措辞、
compressed_stream 多格式过度承诺、并发同步缺注释、采样/读取逻辑重复、散落未跟踪文档。

修复后确立基线：`reader`(解析+帧累积) -> SPSC(深度4) -> `writer`(编码+zstd+xxh64+写盘)，
`yield` 轮询 + 裸 `std::thread` + `close`/`abort` 双 bool shutdown。SPSC 已修过丢尾帧竞态。

## 路线图 A -> H

阶段明细与逐阶段验证数据见 `docs/roadmap.md`；此处只留每步的叙事。

### A：阻塞同步（CV + mutex）

`push`/`pop` 满/空时 `yield` 自旋改为 `condition_variable` + `mutex` + predicate `wait`。
`close` = drain（`notify_all` 唤醒所有等待者）。陷阱：notify 在解锁后；`wait` 用 predicate
版本防虚假唤醒。选 CV+mutex（非 semaphore）：CV+shutdown 是并发最经典难题，且 predicate
wait 与 B 阶段 stop_token 集成自然。

### B：现代取消（jthread + stop_token）

裸 `std::thread` + 手动 `abort()` bool 改为 `std::jthread` + `stop_source`。`push`/`pop`
加可选 `std::stop_token` + `condition_variable_any`（stop 时经 CV 唤醒，不自旋）。`close`
（drain）与 `stop`（cancel）分离。环境升级 libc++ 19 -> 21 解锁 jthread（Ubuntu libc++-19
禁用了 stop_token，`__cpp_lib_jthread` 未定义）。

### C：3-stage 流水线

`writer.writeFrame` 拆成 free `encodeFrame`（measure + 2-bit 打包 + checksum，CPU 密集）+
`ArchiveWriter::writeEncodedFrame`（zstd + 写盘，I/O 密集）。`reader -> q1 -> encoder -> q2 ->
compressor`，shutdown 链式 close（reader close q1 -> encoder 见 nullopt close q2 -> compressor
见 nullopt）。揭示：encode 与 zstd 可分离，写盘成新瓶颈。

### D：多帧并行编码 + reorder

`reader -> [MPMC] -> N encoder 并行 -> [MPMC] -> writer(reorder 按帧序写)`。新增
`MpmcQueue`（mutex + `condition_variable_any` + `stop_token`，多生产多消费）与
`ReorderBuffer`（乱序完成按单调 `frameId` 有序提交）。`frameId` 由 reader 单线程分配，
reorder 恢复顺序，保证 archive 的单调帧 ID 契约（磁盘帧 ID = writer 的 `stats_.frameCount`，
解码侧校验递增）。`encoderError` 用 mutex 保护（N encoder 竞争）；join 顺序
`reader -> encoders -> close queue2 -> compressor`。

### E–H：测量、下沉、镜像、数据并行

D 之后按测量推进，不再猜瓶颈：

- **E** 补上 relaxed 队列计数器、分段计时和 A/B 同窗口脚本，把 WSL2 噪声量化成约 ±5% 的可分辨阈值。
- **F** 把 zstd 从 writer 迁入 encoder worker，writer 退化为纯 I/O；归档与下沉前逐字节一致。
- **I** 质量流单独提档，拟真与随机质量上都未过「体积 ≥3% 且吞吐回退 ≤10%」门槛，默认保持 level 1。
- **G** 解压做成压缩的镜像三段流水线；`verify` 换空 `RecordSink` 复用同一条路径。
- **H** 对未压缩普通文件做字节块并行解析。短读压缩 +47.7%；长读解析占比小，收益落在噪声内。对齐协议曾静默丢弃非首块畸形记录，已按 critical 复盘修好。

## 性能结论

N=4 并行编码**未带来近线性提升**。诊断（详见 `docs/postmortems/2026-07-26-stage-d-parallel-encode-no-speedup.md`）：

- 表层：WSL2 吞吐波动 20-85%（同代码同配置两次跑差一倍），单次数字不可信。
- 深层：Amdahl——当时 encoder 非瓶颈。reader（单线程解析）与 compressor（单线程 zstd+IO）才是。

后续阶段按这个诊断收口：F 拿掉 writer 侧 zstd，H 并行化未压缩解析。短读路径的收益能从 A/B 同窗口里分开；长读和 gzip 不在 H 的范围内。合成数据压缩比在 H 后仍约 2.96× / 2.84×（illumina / ont）；真实语料见 `docs/real-corpus.md`。

## 验证方法

每阶段：clang-debug 全绿 + **clang-tsan 无竞争**（核心）+ clang-format + `test_performance.sh`
基线。不稳定环境下用"验证三角"判无回归：

1. 正确性：压缩比一致（reorder 不改内容）。
2. 并发正确性：clang-tsan 无竞争。
3. 性能同态对比：切代码同状态重跑（D-NOW vs C-NOW），而非跨时比基线（排除机器漂移）。

## 收获

1. 一次一个并发知识点，每步 tsan 验证可回退。
2. 并发优化前先 profiling--盲目并行非瓶颈 stage 会落空（Amdahl 教训）。
3. WSL2 不适合精细性能归因，吞吐波动淹没代码差异。
4. 抽象克制：有第三个重复模式再抽泛型，别预先造框架。
5. 不稳定环境靠多证据交叉判无回归，不靠单次性能数字。
6. 课程有终点：A–H 收束后不再为了练而叠阶段；下一步是真实语料验收，而不是阶段 J。

## 2026-11：从“调压缩比参数”转向“最高压缩比结构研究”

并发课程 A–H 收束后，问题从“流水线还能怎么快”转成了“压缩比还能不能明显提升”。一开始的
直觉是调 zstd 档位、质量值 delta、帧大小；实测后否掉了这条轻量路线：

- 本地真实 HiSeq 小样本（25k reads、101 bp）当前 FQC 约 4.0006× 原始；质量流占归档约 51%，
  序列约 42%，ID 约 7.5%。
- 序列 2-bit 打包后 zstd 几乎不再缩小；帧大小 1/2/4/8 MiB 对压缩比几乎没有影响。
- 质量值 delta/transpose/6-bit 在真实 HiSeq 上大多更差；合成数据上的 −18% delta 收益
  不能外推。
- 2025 年独立 benchmark 与 ORA/Genozip/SPRING/PgRC2/Mstcom/FQSqueezer/fqxv 的对比说明：
  10×–20× 区间来自参考映射、全局伪基因组/重排、质量上下文编码，不是通用压缩器调参。

因此把“最高压缩比”从 fast 路径的优化项里拆出来，作为独立专项：`max-ref` 追求有参绝对
最高压缩比，`max-ref-free` 追求无参最高压缩比；默认保序、不存 permutation，质量值优先
复用 htscodecs fqzcomp（策略 0/1 自适应，zstd 回退）。

2026-11-15 落地 Phase 0 测量底座 `fqc_ratio_probe`：独立 byte-preserving scanner，保留
`+` 行、CRLF/LF、空 read 和无最终换行，输出 `fqc-ratio-probe/v1` JSON；开发目标不安装，
不进入主 CLI。真实 HiSeq 基线与 SHA 见
[docs/max-ratio-baselines/2026-11-15-hiseq.md](max-ratio-baselines/2026-11-15-hiseq.md)，
工具说明见 [tools/ratio_probe/README.md](../tools/ratio_probe/README.md)。

同一天用 htscodecs `v1.6.7` 做 Phase 1 质量 codec 探针 `fqc_quality_probe`：
HiSeq 全质量 fqzcomp 558,189 B vs zstd19 678,308 B（−17.7%）；NovaSeq
`DRR174812` 分箱质量头部 2,159,788 B vs 2,802,326 B（−22.9%）；WGS
`SRR14724532` 分箱质量头部 2,168,649 B vs 2,823,235 B（−23.2%）。按当前 FQC 归档
只替换质量流估算，三者总归档都可缩约 15.5%–16.6%；但这是估算，不是已实现的 max-ratio 归档。
数据集 manifest 见 [datasets.md](max-ratio-baselines/datasets.md)，完整实验见
[2026-11-15-quality-codecs.md](max-ratio-baselines/2026-11-15-quality-codecs.md)，
工具见 [tools/quality_probe/README.md](../tools/quality_probe/README.md)。
随后把质量 codec 落成 Phase 1 实验容器 `fqc-max/v1`：独立 magic/头部/footer，ID 与
2-bit sequence 复用 v2 逻辑流，质量按块在 fqzcomp 与 zstd19 间取较小者。真实 HiSeq /
NovaSeq 头部 / WGS 头部归档分别为 1,285,285 / 12,280,644 / 12,828,169 B，相对当前 v2
−21.36% / −24.87% / −23.69%，对应 5.087× / 8.165× / 7.790× raw；`max-verify` 与解压
后 `cmp` 均通过。见
[2026-11-15-max-container.md](max-ratio-baselines/2026-11-15-max-container.md)。

Phase 2 继续把“参考”从设计变成可执行原型：开发工具 `fqc_ref_archive` 读取
FASTQ + external reference FASTA + PAF sidecar，把 mapped read 编码为 contig
id/target offset/方向/mismatch delta，未映射或非纯 `<len>M` read 回退 literal；质量流
仍按块在 fqzcomp 与 zstd19 间取较小者。5 Mb 随机参考 + wgsim 3× 双端 150 bp 上，
R1 17,423,235 B → 827,688 B（21.050× raw），R2 → 827,546 B（21.054× raw），
`verify` 与解压 `cmp` 通过，重复压缩 SHA 一致。但这只是机制验证：参考不计入、质量恒定
Q23、随机参考、只接受纯 M 比对，不是人类 30× WGS 的第一梯队结论。见
[2026-11-15-ref-container.md](max-ratio-baselines/2026-11-15-ref-container.md)、
[Phase 2 格式契约](../openspec/changes/2026-11-15-max-ratio-phase2-ref/design.md)。

Phase 2.1 把 ID 流从“整段 raw + zstd”改成自适应 front coding：每块同时尝试 raw 与
“与上一条记录的公共前缀 + 后缀”两种逻辑流，各自 zstd19 后取较小者。真实 WGS/NovaSeq
头部 ID 探针约 −99%，随机 wgsim ID 自动回退 raw；人工结构化 ID 合成 R1 为 331,553 B
（53.385× raw），`verify`/`cmp` 通过。这个数字仍是合成机制验证，不是真实 WGS。见
[ID codec 基线](max-ratio-baselines/2026-11-15-id-codec.md)、
[ID codec 契约](../openspec/changes/2026-11-15-max-ratio-phase2-id-codec/design.md)。

但 `--max-ratio` 顶层开关、内置 mapper/BAM、参考嵌入、clip/indel、Phase 3 伪基因组与
`+` 行/CRLF/空 read 的逐字节保真仍未实现；两个实验容器/原型都只覆盖 canonical LF
FASTQ。

- 设计、证据矩阵与本地实验：[docs/max-ratio-design.md](max-ratio-design.md)
- 决策：[ADR-0003](decisions/0003-max-ratio-mode.md)
- 参考原型实测：[docs/max-ratio-baselines/2026-11-15-ref-container.md](max-ratio-baselines/2026-11-15-ref-container.md)
- 参考原型契约：[openspec/changes/2026-11-15-max-ratio-phase2-ref/design.md](../openspec/changes/2026-11-15-max-ratio-phase2-ref/design.md)
- 参考原型笔记：[.agents/notes/implemented/architecture/2026-11-15-max-ratio-phase2-ref.md](../.agents/notes/implemented/architecture/2026-11-15-max-ratio-phase2-ref.md)
- ID codec 实测：[docs/max-ratio-baselines/2026-11-15-id-codec.md](max-ratio-baselines/2026-11-15-id-codec.md)
- ID codec 契约：[openspec/changes/2026-11-15-max-ratio-phase2-id-codec/design.md](../openspec/changes/2026-11-15-max-ratio-phase2-id-codec/design.md)
- ID codec 笔记：[.agents/notes/implemented/architecture/2026-11-15-max-ratio-phase2-id-codec.md](../.agents/notes/implemented/architecture/2026-11-15-max-ratio-phase2-id-codec.md)
- 提案笔记：[.agents/notes/proposed/architecture/2026-11-14-max-ratio-mode.md](../.agents/notes/proposed/architecture/2026-11-14-max-ratio-mode.md)
- 旧调研的适用范围修正：[docs/fastq-compression-survey.md](fastq-compression-survey.md)
- Phase 0 测量底座提案：[openspec/changes/2026-11-14-max-ratio-phase0-benchmark](../openspec/changes/2026-11-14-max-ratio-phase0-benchmark/proposal.md)
- 质量 codec 提案：[openspec/changes/2026-11-15-max-ratio-quality-codec](../openspec/changes/2026-11-15-max-ratio-quality-codec/proposal.md)
- Phase 1 容器契约：[openspec/changes/2026-11-15-max-ratio-phase1-container](../openspec/changes/2026-11-15-max-ratio-phase1-container/proposal.md)
- 实验容器基线：[docs/max-ratio-baselines/2026-11-15-max-container.md](max-ratio-baselines/2026-11-15-max-container.md)
- 容器实现笔记：[.agents/notes/implemented/architecture/2026-11-15-max-ratio-phase1-container.md](../.agents/notes/implemented/architecture/2026-11-15-max-ratio-phase1-container.md)
- 数据集 manifest：[docs/max-ratio-baselines/datasets.md](max-ratio-baselines/datasets.md)
