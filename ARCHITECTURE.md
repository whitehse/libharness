# ARCHITECTURE.md — libharness

## Module Boundaries
- **harness.c**: Core state machine (explicit states: INIT, READY, PROCESSING_OPENAI, TOOL_CALL, LOOPING, LOGGING, VECTOR_OP, ERROR). Pure, no syscalls. Event queue, extension registry. Session-oriented bookkeeping is data + events only (ADR 002).
- **lua_bindings.c**: Lua C API integration. Exposes harness characteristics to Lua scripts. Lua defines deterministic behavior, tools, loop criteria, SOUL assembly, Honcho mirror policy, secret redaction policy. Scripts loaded from PG or memory (no local files preferred).
- **openai_processor.c**: OpenAI-compatible context builder and response parser. Identity-prefixed multi-participant messages, dynamic tools + SOUL injection. JSON payloads only; transport in caller (librest/shaggy or other providers). Normalizes requires_action vs completed (ADR 002).
- **honcho_interface.c**: Abstraction over Honcho workspace/peer/session/message graph for long-term facts, conclusions, session context. Peer ids are participant ids. Tool calls not mirrored by default.
- **pique_integration.c**: libpique (PostgreSQL wire protocol) integration. Logs every model interaction. Uses pg_vector for:
  - Token-reducing classification of state/data
  - Personality embeddings and similarity search
  - Local memory vectors
  All persistence via PG; local files minimized.

## Domain model (see ADR 002, DOMAIN.md)
- **Session**: durable multi-party thread; humans/apps/agents as peer participants.
- **Model**: context → response; tools and explanations.
- **Context**: model, temperature, tools, messages (system/developer SOUL, user, assistant, tool).
- **Response**: completed message and/or function_call actions + usage.
- **Honcho**: Workspace → Peers & Sessions → Messages; conceptually joined peer ids; optional at deploy.

## Invariants
- No sockets, files, malloc in hot paths unless caller-controlled.
- All I/O (network to model providers, PG wire via pique, Honcho) owned by calling application or explicit context.
- Lua is the policy layer; C is the plumbing (ADR 006 style).
- Extensions (C functions or Lua modules) registered at well-defined points without polluting core state machine.
- Event-driven preferred: feed_input → next_event → get_output.
- Secrets appear as references to non-privileged participants; core does not store secret material by policy default.
- Tool call payloads are first-class in the OpenAI processor path and PG logs; they are not default Honcho messages.

## Deliberate Absences
- No built-in HTTP client or OpenAI SDK (network in caller).
- No direct file I/O for scripts or logs (PG preferred).
- No hard-coded models or personalities (dynamic via Lua + PG vectors).
- No callbacks; explicit events only.
- No automatic tool execution or automatic Honcho POSTs inside core.

## Extension Points
- harness_register_extension (C)
- Lua: harness.register_tool, harness.register_loop_criterion, session/participant helpers (to be implemented)
- Sibling library APIs (libpique, librest, shaggy, libjsparse for JSON, etc.) consumable from Lua via FFI or bindings.

This design keeps the harness light, flexible, and true to the sibling library philosophy while enabling powerful Lua-orchestrated multi-participant AI sessions with persistent memory and vector intelligence.
