# Agent Note: 修复 CI 的模块扫描、clang-tidy 与 GTest ASan 误报

Status: implemented

## Problem

合并 max-ratio 实验分支后，`master` 的 CI 在 `clang-tidy` 与 `sanitizer` 两个 job 变红：

1. **clang-tidy 在所有文件上先报 `no such file or directory: @...o.modmap`**。
   CMake 3.28 + Clang 会为 C++23 生成模块扫描响应文件；clang-tidy job 只配置不构建，
   `.modmap` 尚未生成，但 `compile_commands.json` 已经引用了它们。
2. **max-ratio 新代码触发 WarningsAsErrors**：`max_ratio.cpp` 的复杂度/魔数/const，
   `quality_codec.cpp` 的魔数/`std::free`/`const_cast`，`max_ratio_engine.cpp` 的
   `std::optional` 跨 `Result` 解引用，`main.cpp` 的 CLI 默认值魔数。
3. **ASan job 在 GTest 1.17.0 + libc++18 的静态注册路径报 container-overflow**：
   栈落在 `testing::internal::UnitTestFilter::~UnitTestFilter()`，由
   `tests/tools/ref_archive_test.cpp` 的 `TEST` 宏触发；项目代码没有越界。

## Decision

- 在 `conan-build` action 的“只配置”路径显式关闭 C++ 模块扫描：
  `-DCMAKE_CXX_SCAN_FOR_MODULES=OFF`。lint 不需要模块扫描，避免生成/引用 `.modmap`。
- 对 max-ratio 新代码做最小修复：
  - 魔数改为具名常量（质量偏移/上限、block header checksum trailer、CLI 默认值）；
  - `Footer parsed` 加 `const`；
  - `readRawBlock` 的二进制解析复杂度保留单函数，加 `NOLINTNEXTLINE` 并说明原因；
  - `std::optional` 跨 `Result` 的解引用按仓库既有模式加
    `NOLINTNEXTLINE(bugprone-unchecked-optional-access)`；
  - fqzcomp 的 `malloc` 缓冲区改用 `unique_ptr` + `FreeDeleter`，只在一个 deleter
    中保留 `std::free` 的 NOLINT；C API 的只读 `char*` 参数集中到一个带 NOLINT 的
    `mutablePayload`。
- CI ASan 的 `ASAN_OPTIONS` 增加 `detect_container_overflow=0`，并注明这是
  GTest 1.17.0 + libc++18 静态注册路径的误报；本地 clang-21 ASan 仍开启该检查。

## Verification

- `./scripts/test.sh clang-debug`：22/22 passed。
- `./scripts/test.sh clang-asan`：22/22 passed；本地未关闭 `detect_container_overflow`。
- `ASAN_OPTIONS=detect_leaks=0:alloc_dealloc_mismatch=0:detect_container_overflow=0 ./scripts/test.sh clang-asan`：22/22 passed。
- 用 libc++18 头文件对 `src/*.cpp` 全量跑 clang-tidy-18：0 条 `src/` 告警。
- `./scripts/lint.sh format-check`、`./scripts/notes.sh verify` 通过。
- 真实 WGS 归档 `SRR14724532_R1.head.fixed.fqmr` 重新 `verify` + `decompress` + `cmp`
  通过，SHA 未变化。

## Alternatives considered

- **clang-tidy job 先完整构建再分析**：能生成 `.modmap`，但把 lint job 变成编译 job，
  CI 时间和缓存复杂度都上升；lint 不需要模块信息，关闭扫描更直接。
- **本地/CI 安装 libc++18 并统一工具链**：会放弃本地 clang-21 的诊断能力，且与
  ADR/既有“CI 与本地版本刻意并存”的决策冲突。
- **降级 GTest 或给 `ref_archive_test` 单独关 ASan**：前者是依赖变更，后者会让测试
  完全绕过 sanitizer；只关闭 CI 的 GTest 误报检查、保留其余 ASan/UBSan 更小。
- **拆分 `readRawBlock`**：二进制解析的状态和错误分支高度耦合，拆分收益低、回归风险高；
  仓库已有对复杂流水线函数使用 NOLINT 的先例。

## Consequences

- `master` 的 clang-tidy 与 sanitizer 门禁恢复可执行；`.modmap` 不再阻塞 lint。
- CI 的 container-overflow 检查被关闭，但本地 clang-21 ASan 仍覆盖该检查；这是一条
  明确记录的门禁差异，不是“CI 全绿等于所有 sanitizer 检查都开启”。
- 真实 WGS 与 max-ratio 归档的字节输出未变化；修复只涉及 CI 配置、lint 可读性与
  fqzcomp 缓冲区的 RAII 管理。
