# Agent Note: Release 门禁与 draft 人工发布

Status: implemented

## Problem

`release.yml` 只在 `v*` tag 上构建并自动发布，tag 与 `VERSION`、CHANGELOG 之间没有
机器校验；tag 写错或版本说明缺失时，错误版本仍可能发布。workflow 还会直接操作
同名 Release，重跑旧 tag 存在覆盖已正式发布产物的风险。

## Decision

- 新增 `validate-release` job：要求 tag 为 `vMAJOR.MINOR.PATCH`，版本号与 `VERSION`
  完全一致，CHANGELOG 存在同名且非空的 `## [版本]` 段。
- 通过 GitHub API 查询同名 Release：不存在或仍是 draft 才继续；已正式发布则失败。
- `release` job 依赖校验 job，并在创建/更新 Release 前再次确认状态。
- `action-gh-release` 固定 `draft: true`；维护者核对 CHANGELOG、产物与 smoke test
  后手动发布。

## Alternatives considered

- **继续 tag 触发即自动发布** — 被否：无法在发布前拦截错误版本，也没有人工核对
  draft 产物的窗口。
- **只靠发布者本地 checklist** — 被否：本地检查不能约束其他维护者或重跑 workflow。
- **保留对已存在 Release 的直接更新** — 被否：正式发布是不可变交付边界，重跑旧 tag
  不得静默替换资产。

## Consequences

- `VERSION` 为 `0.2.0`，但 CHANGELOG 暂无 `[0.2.0]` 段；在补齐发布说明前，`v0.2.0`
  会被门禁按设计拒绝。
- 发布流程从“tag 即上线”变为“tag 生成 draft，人工核对后发布”，多一步维护动作，
  换取版本与资产的一致性。
- 已发布 Release 不会被重跑 workflow 覆盖；需要修复已发布版本时，应发布新版本。

## Verification

- `release.yml` PyYAML 解析通过；`git diff --check` 通过。
- 用 workflow 原始 shell 片段和 `gh` 模拟器覆盖：合法 tag、版本不匹配、非法 tag、
  CHANGELOG 缺失/空段、Release 不存在、draft、已发布、API 错误。
- 未创建真实 tag 或 Release；远端 `v0.1.0` 仍为已发布状态，未被修改。
