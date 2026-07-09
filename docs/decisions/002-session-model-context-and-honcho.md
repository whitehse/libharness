# ADR 002: Session, Model, Context, Response, and Honcho Integration

**Date**: 2026-07-09
**Status**: Accepted
**Deciders**: Project maintainers (design refinement + Hermes agent)

## Context

ADR 001 bootstrapped libharness as a pure-plumbing C + Lua AI harness with stubs for OpenAI-compatible processing, Honcho memory, and PostgreSQL (libpique / pg_vector). The bootstrap left the domain model underspecified: what is a long-lived multi-party interaction, how identities map to model inputs, what a “context” is, how tool calls and secrets are treated, and how Honcho’s workspace/peer/session graph joins the harness.

Without an explicit domain model, subsequent OpenAI processor, memory, and logging work risk treating the harness as a single-user chat loop, which does not match intended multi-participant use (humans, apps, and agents as peers in a durable session).

This decision records the design refinement for those concepts so implementation can proceed against stable vocabulary and boundaries.

## Decision

libharness shall treat the following domain concepts as first-class and shape its C plumbing and Lua policy surfaces accordingly.

### 1. Session

A **session** lives forever (or until explicitly retired by the calling application). It is analogous to a threaded chat (e.g. Slack thread):

- A session may include any number of **participants**: humans, apps, or agents.
- Each participant makes independent decisions and takes its own actions.
- Each participant has motivations and responsibilities to care for.
- Within a session, each participant can see all messages sent by other participants, with one exception:
  - Passwords and secrets must not be shared with all participants. Non-privileged participants see a **hidden reference** (presence of a secret is known or linked; value is not).
- Generally, all participants are **peers**, whether human or artificial.
- By participating, parties agree to adhere to the conventions and rules of that session.

Harness implications:

- The unit of multi-party interaction is the session, not a single user↔assistant turn.
- Participant identity is stable and addressable (see Honcho peer id below).
- Core library does not enforce session rules or secret policy (plumbing); Lua and the caller apply policy. Core must be able to represent identity-prefixed messages, privilege boundaries as opaque metadata, and multi-participant message sequences without assuming a single human.

### 2. Model

A **model** responds, reasons, judges, makes decisions, uses tools, and explains itself. It takes a **context** as input and produces a **response** (output).

Harness implications:

- The library builds/parses context and response structures (JSON / events); the caller owns transport to providers (OpenAI-compatible HTTP via librest/shaggy, Ollama, vLLM, or other).
- Tool invocation is observed as structured events; execution of tools is policy (Lua / caller), not hidden I/O in the C core.

### 3. Context

A **context** is a prepared JSON string (or equivalent structured form prior to serialization) sent to an OpenAI API compatible provider, an Ollama provider, a vLLM provider, or another model provider.

A context object contains at least:

- `model`
- `temperature`
- `tools` (optional array of tools available to the model)
- one or more `messages`

#### Tools in context

Tool entries follow the common function-tool shape, for example:

```json
"tools": [
  {
    "type": "function",
    "function": {
      "name": "get_current_weather",
      "description": "Get the current weather for a given location",
      "parameters": {
        "type": "object",
        "properties": {
          "location": {
            "type": "string",
            "description": "The city and state, e.g. San Francisco, CA"
          },
          "format": {
            "type": "string",
            "enum": ["celsius", "fahrenheit"],
            "description": "The temperature unit to use"
          }
        },
        "required": ["location"]
      }
    }
  }
]
```

Recognized tool type categories (provider-dependent):

- `function` — general callable tools (primary harness path; Lua/C-registered tools map here)
- `code_interpreter` — provider-specific sandboxed code execution (e.g. OpenAI Assistants)
- `file_search` — provider-specific retrieval over uploaded files
- `web_search` — provider-specific web retrieval

The core shall treat unknown tool types as opaque structured data when parsing/serializing so new provider tools do not require core changes.

#### Messages in context

A message contains a `role` of one of:

| Role | Meaning |
|------|---------|
| `system` | Privileged instructions to the model; this is the **SOUL** sent to the model |
| `developer` | Alias for system |
| `user` | Something a human (or human-facing participant channel) says |
| `assistant` | Something an AI agent says |
| `tool` | Output of a tool call made by a model |

#### Tool calls (assistant → tools)

An assistant turn that requests tools has the form:

- `"role": "assistant"`
- `"content": null` (when only tool calls are present)
- `"tool_calls"`: array of tool calls

Each tool call includes:

- `id` (e.g. `call_beta_001`)
- `type` (e.g. `function`)
- `function.name` and `function.arguments` (arguments as a JSON string per OpenAI-compatible conventions)

Namespaced tool names are allowed (e.g. `python_interpreter:run_code`, `database:query_inventory_logs`).

#### Identity prefix for session context

In a **session** context, every message content presented to the model shall be **prepended with the identity that created the message**, for example:

```text
[Human_Alice]: Can someone help me analyze this quarterly sales drop?
```

- That identity is presented to Honcho as a **peer id**.
- In libharness, participants generally correspond to a Honcho peer id.
- Honcho may be an **optional** memory provider, but the two systems are **conceptually joined**: participant identity in the harness and peer identity in Honcho share the same namespace in multi-party sessions.

### 4. Response

A model **response** is structured output from the provider (OpenAI-compatible “response” / chat completion shapes may both appear; the processor normalizes to events). Canonical fields include:

- `id`
- `object` (e.g. `"response"`)
- `created` (unix timestamp)
- `model`
- `status` — at least:
  - `"requires_action"` — model requested tool work; `output` carries function call items
  - `"completed"` — final assistant message content available
- `output` — array of items such as:
  - `type: "function_call"` with `call_id`, `name`, `arguments`
  - `type: "message"` with `role: "assistant"` and text (or multi-part) content
- `usage` — `prompt_tokens`, `completion_tokens`, `total_tokens` when provided

Harness implications:

- Processor emits structured events for completed messages vs required tool actions.
- Token usage is available for logging (PostgreSQL via libpique) and for loop / compression policy in Lua.

### 5. Honcho integration model

Honcho’s conceptual model maps onto harness sessions as follows:

| Honcho | Harness correspondence |
|--------|------------------------|
| **Workspace** | Top-level isolation boundary (namespace for applications/environments) |
| **Peer** | Participant (human, app, or agent); stable identity / peer id |
| **Session** | The durable multi-party session |
| **Message** | Content sent by a peer into a session |

Rules:

- A Workspace has Peers and Sessions.
- A Peer can be in multiple Sessions and can send Messages in a Session.
- A Session can have many Peers and stores Messages sent by its Peers.
- A Workspace provides complete isolation between different applications or environments.

**Tool calls are typically not passed to Honcho.** Chat / agent narrative messages are. The caller and Lua policy decide what is logged to Honcho vs PostgreSQL interaction logs; the library must not force tool call payloads into Honcho by default.

Example peer messages (illustrative only; transport and credentials live outside the library):

- `human_alice` — chat_message
- `human_bob` — chat_message
- `agent_devbot` — agent_response with optional model metadata

### 6. Plumbing boundary (restated for this domain)

Consistent with ADR 006-style sibling libraries:

- Core builds/parses contexts and responses, tracks session-oriented message sequences as buffers/events, and exposes hooks for identity, tools, and SOUL injection.
- Core does **not** open network connections to model providers or Honcho, write files, or execute tools.
- Lua defines personality (SOUL assembly), tool registration, loop criteria, and which messages are mirrored to Honcho vs PG.
- PostgreSQL (libpique) remains preferred for durable interaction logging and pg_vector classification; Honcho remains the peer/session memory graph (optional at deploy time, first-class in the domain model).

## Rationale

- Multi-participant forever-lived sessions match real collaborative harness use better than single-turn chat wrappers.
- Identity-prefixed messages give models clear speaker attribution without inventing a non-standard API.
- Aligning participant ids with Honcho peer ids avoids dual identity systems.
- Keeping secrets as references and excluding tool calls from default Honcho storage limits leakage and noise while preserving auditability via PG logs.
- Explicit OpenAI-compatible context/response shapes keep the processor portable across providers.

## Consequences

### Positive

- Clear vocabulary for DOMAIN.md, Lua APIs, and event types (session join, message with peer id, context ready, tool action required, response completed).
- Implementation of `openai_processor`, `honcho_interface`, and logging can proceed without re-litigating multi-party semantics.
- Privilege and secret handling can be tested as policy without baking crypto or secret stores into core.

### Negative / costs

- Context assembly is more than a flat chat history: identity prefixes, multi-peer ordering, and secret redaction must be designed into the processor and Lua layer.
- Provider response shape variance (chat completions vs Responses API vs Assistants tools) requires a normalization layer in the processor.
- Optional Honcho means the harness must work with peer ids even when Honcho is not configured (local/PG-only identity table or caller-supplied ids).

### Follow-on work (not part of this ADR’s implementation)

- Event types and C/Lua APIs for session participants and identity-prefixed message append.
- Context builder that injects SOUL (`system`/`developer`), tools, and prefixed history.
- Response parser emitting requires_action vs completed events.
- Honcho message mirror policy (exclude tool calls by default).
- Secret/reference redaction hooks (Lua or registered extension).

## Verification

- Documentation: DOMAIN.md, ARCHITECTURE.md, and AGENTS.md describe Session, Model, Context, Response, and Honcho as above.
- Future code: unit/dialectic-style tests feed multi-participant message sequences into context build and assert identity prefixes and tool-call exclusion from Honcho-bound payloads; no syscalls in core.

## References

- ADR 001 — Bootstrap libharness as C/Lua AI Harness
- Sibling ADR 006 — Core library as plumbing (philosophy)
- design_refinement.txt — source design opinions for this decision
