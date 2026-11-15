# Agent Note: CI 提效三修剪——文档豁免、tidy 瘦身、conan 包缓存

Status: implemented

## Problem

对 `gh run` 实测数据（2026-10-01 run 36873592388）的效率审计发现三处浪费：

- **纯文档提交触发全量 CI**：单次 run 约 21 runner-minutes（8 job 之和），而
  git 近期提交里 docs-only 占比约四成；`docs/**`、`**.md` 变更没有任何对应
  门禁，8 个 job 全跑一遍是纯浪费。
- **clang-tidy job 为一个 JSON 文件编译整个项目**：该 job 5m24s（并列关键
  路径），其中 `conan-build` 完整编译 clang-debug 只为产出
  `compile_commands.json`（configure 阶段即生成）；ccache 命中率仅 7%
  （2/27），`tidy-*` 缓存基本无效。串行 while 循环逐文件跑 tidy 进一步拉长。
- **每个 job 每次运行都从源码构建 conan 依赖**：conan install 阶段
  `zstd` 等因 cppstd=23 + clang-18 无预编译包而现场构建（日志可见
  `Building C object libzstd_static`），8 job × 30-60s/次，无任何缓存。

## Decision

- **`ci.yml` 触发器加 `paths-ignore`**（push 与 pull_request 同配置）：
  豁免 `**.md`、`docs/**`、`openspec/**`、`perf-baselines/**`。刻意**不**
  豁免 `.agents/**`——notes 封印校验（`notes` job，
  见 2026-09-27-agent-decision-notes）必须对 `.agents/notes` 的任何变更
  保持运行；改动归档笔记的提交因此永远触发 CI。
- **clang-tidy job 改为 configure-only + 并行**：`conan-build` action 新增
  `run-build` 输入（默认 `'true'` 保持既有调用不变），为 `'false'` 时止步于
  `cmake --preset`（`CMAKE_EXPORT_COMPILE_COMMANDS=ON` 在 configure 期
  即产出 compile DB；src/tests 均无 `configure_file`/构建期生成头文件，
  configure 产物对静态分析完整）。job 内去掉 ccache 步骤（无编译可加速）；
  9 个 `src/*.cpp` 用 `xargs -P "$(nproc)" -n 1` 每核一进程并行分析。参数
  传递形态固定为 `sh -c 'exec clang-tidy-18 … "$1"' _`（不带 `-I`，输入项
  即 `$1`）——曾在 smoke test 里发现多写占位 `{}` 会抢占 `$1`、真实文件名
  落到 `$2` 的坑。门禁语义不变：任一文件失败 xargs 以 123 退出，配合
  `set -e` 整体失败。
- **conan-build action 增加 `~/.conan2` 包缓存**（`actions/cache@v4`）：
  key 绑定 `hashFiles('conan.lock')`，按 settings 分组
  （`libcxx` + `build_type`）而非按 preset——相同 settings 的 preset（如
  clang-debug 与 clang-coverage）共享同一份缓存，`restore-keys` 兜底部分
  命中。该 action 为 ci.yml 全部 job、release.yml、fuzz-nightly.yml 共用，
  缓存自动覆盖所有调用方。

## Alternatives considered

- **自建容器镜像预装 clang-18 工具链**（消灭每 job 的 apt update + install
  与 pip install conan，约 5-8 runner-min/run）— 最强的对手方案：收益比
  本篇的包缓存更大。暂不采纳：需要维护 Dockerfile + 镜像构建发布流水线
  （ GHCR + 独立 workflow），基建成本对个人项目当前用量不成比例；若后续
  CI 用量显著上升，此路保留为首选。
- **fuzz job 双目标并行**（`&` + `wait`）— 每次 run 省 ~90s 墙钟，但
  billed runner-minutes 不变（同样的 CPU 预算），且 90s/目标的串行时长是
  2026-10-01-ci-gates-and-toolchain 定下的门禁语义（每目标独立日志），
  不在本篇动。
- **paths-ignore 只豁免 push、保留 PR 全量** — 无收益：本项目目前全部
  直推 master（近 15 次运行无一 PR 事件），PR 侧豁免与 push 侧保持一致
  才能在将来开 PR 流程时同样受益。

## Consequences

- **收益**：docs-only 提交从 ~21 runner-minutes 降为 0；clang-tidy job
  从 ~5.5 min（含全量编译）降为预计 ~2 min（conan install + configure +
  并行分析），且不再消耗 `tidy-*` ccache 缓存配额；依赖构建在缓存命中后
  各 job 省 30-60s。fuzz 成 wall-clock 新关键路径（~5 min，90s/目标 × 2
  为既定门禁预算）。
- **代价**：纯文档提交不再触发任何门禁——可接受，因为这些路径没有门禁
  可损失；`.agents/notes` 变更仍全量触发，封印校验无盲区。conan 缓存引入
  一个新缓存条目（每 settings 组一份，仓库缓存配额 10 GB 内占用极小），
  且缓存命中与否不影响构建正确性（miss 时行为与现状完全一致）。
- **验证边界**：本地工具链为 clang-21，无法完整复现 runner 行为；
  configure-only 产出的 compile DB 完整性最终由下一次 CI 运行确认（
  clang-tidy 对 9 个 src 文件逐一给出分析结果即为通过）。

## Verification

- 两份 YAML 均通过 python-yaml 解析（本地无 actionlint）。
- `xargs -P … -n 1 sh -c 'exec … "$1"' _` 管道在本地以 echo 替身 smoke
  test：9 个文件全部作为 `$1` 正确传入、并行执行、退出码正确。
- 本地 `build/clang-debug/compile_commands.json`（25 条目）由 configure
  阶段产出、覆盖全部 src/*.cpp，证实 configure-only 路径可行。
- 待 CI 实跑确认：paths-ignore 生效性、tidy job 新流程绿灯、conan 缓存
  命中率。
