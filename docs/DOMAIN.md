# DOMAIN.md — AI Harness Concepts

Primary domain model: **ADR 002** (Session, Model, Context, Response, Honcho).

## Session

A session lives forever (until the application retires it). Like a threaded chat:

- Any number of **participants** (humans, apps, agents).
- Participants decide and act independently; each has motivations and responsibilities.
- All participants see all messages, except **secrets/passwords**: non-privileged peers see a hidden **reference**, not the value.
- Participants are generally **peers** (human or artificial) and agree to session conventions/rules.

Participant identity aligns with a **Honcho peer id** (conceptually joined even when Honcho is optional at deploy time).

## Model

A model responds, reasons, judges, decides, uses tools, and explains itself. **Context in → response out.** Transport to the provider is the caller’s job (librest/shaggy, Ollama, vLLM, etc.).

## Context

Prepared JSON (or structured form before serialize) for OpenAI-compatible and related providers.

Fields: `model`, `temperature`, optional `tools`, one or more `messages`.

### Tools

- Primary: `type: "function"` with name, description, JSON Schema parameters.
- Also recognized (provider-specific, often opaque to core): `code_interpreter`, `file_search`, `web_search`.
- Namespaced names allowed (e.g. `database:query_inventory_logs`).

### Message roles

| Role | Meaning |
|------|---------|
| `system` / `developer` | Privileged instructions — the **SOUL** |
| `user` | Human / human-channel utterance |
| `assistant` | Agent utterance (may include `tool_calls`) |
| `tool` | Result of a tool invocation |

Assistant tool-call turns: `content` may be null; `tool_calls[]` with `id`, `type`, `function.name`, `function.arguments` (arguments string).

### Identity prefix

In session context, message text shown to the model is prepended with creator identity:

```text
[Human_Alice]: Can someone help me analyze this quarterly sales drop?
```

That identity is the peer id used with Honcho and within libharness participant bookkeeping.

## Response

Provider output normalized by the processor. Typical fields: `id`, `object`, `created`, `model`, `status`, `output`, `usage`.

- `status: "requires_action"` — function/tool call items in `output`
- `status: "completed"` — assistant message content
- `usage` — prompt/completion/total tokens when present

## Honcho

| Honcho concept | Role |
|----------------|------|
| Workspace | Top-level isolation namespace |
| Peer | Participant; can join many sessions |
| Session | Durable multi-party conversation |
| Message | Content from a peer in a session |

- Tool calls are **typically not** sent to Honcho.
- Narrative chat / agent responses are appropriate Honcho messages (with optional metadata such as model name).
- Full model interaction audit trail prefers **PostgreSQL** via libpique (plus pg_vector classification).

## Harness characteristics (Lua-exposed)

- Dynamic tool list (`enumerate_tools`, `register_tool`)
- Dynamic personality / SOUL (`set_personality`; vectors + Honcho)
- OpenAI-compatible processor (build context, parse response, tool_calls)
- Loop control (`should_loop` — Lua / PG-backed criteria)
- Deterministic Lua behavior scripts
- Lua tools calling sibling APIs (pique, rest, etc.)
- Honcho memory interface (optional provider, first-class domain peer ids)
- PG logging + pg_vector for token-efficient state reduction
- Session participant / identity-prefix helpers (per ADR 002)

## Key workflows

1. Initialize harness with pique_ctx, optional honcho_ctx, Lua state, session/workspace identity.
2. Register or load participants (peer ids); load SOUL/personality (PG vector + Honcho as available).
3. Enumerate tools (Lua + registered).
4. Append multi-party messages with identity prefixes; redact secrets to references for non-privileged views.
5. Build context (model, temperature, tools, messages) for the acting agent’s turn.
6. Caller sends context to provider; feeds response buffers back.
7. Parse response: completed message and/or requires_action tool calls.
8. Execute tools in Lua/caller policy; append `tool` role results; decide loop vs exit.
9. Log full interaction to PG; mirror non-tool narrative messages to Honcho when configured.
10. Optionally classify/compress state with pg_vector.

All model calls logged. Personalities and long-term peer memory prefer PG embeddings + Honcho graph; local files minimized.

## Privilege capabilities (v0.4)

Participants may carry a capability bitset (`HARNESS_CAP_SEE_SECRETS`,
`MIRROR_HONCHO`, `ADMIN`, `INVOKE_TOOLS`). Legacy `privileged=true` grants
SEE_SECRETS (+ common agent defaults on add). Secret redaction uses
`harness_peer_can_see_secrets` against the acting peer.

Kind-specific SOUL text (`harness_soul_set_for_kind`) is injected as an extra
system message when the acting peer matches that kind.
