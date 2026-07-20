# Fiber explain tool contract (`fiber_explain_fill`)

## Purpose

Employees ask how fiber paths work or why an outage occurred. The model must
**not** emit drawing code. It calls a host tool that fills a vetted libanim
template and returns a validated scene plan.

## Tool definition

| Field | Value |
|-------|--------|
| name | `fiber_explain_fill` |
| type | function |
| parameters | `{ template: string, params: object }` |

Registered in tests via `tests/test_explain_tool.c`. Hosts should register the
same schema before `harness_context_build(..., include_tools=true)`.

## Host execution

1. Look up `fixtures/templates/{template}.tmpl` (or edgehost `spa/explain/templates/`).
2. Flatten `params` to `key=value\\n` lines.
3. `anim_template_fill` + `anim_load_plan` (or `POST /api/v1/explain/render`).
4. Return plan text + duration to the model / SPA player.

## Soul guidance (recommended)

> You explain fiber plant issues for cooperative employees. Always use
> `fiber_explain_fill` with a known template id (`optical_path`, `outage_story`);
> never invent topology or emit C/JS/WebGPU code.
