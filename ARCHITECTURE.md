# ARCHITECTURE.md — libharness

## Module Boundaries
- **harness.c** + **src/harness_internal.h**: Core state machine (INIT, READY, PROCESSING_OPENAI, TOOL_CALL, LOOPING, LOGGING, VECTOR_OP, ERROR). Structured event queue. Session/participant/message/SOUL/tool slots (ADR 002). Pure, no syscalls.
- **lua_bindings.c**: Lua C API integration (`harness_lua_init`). Exposes policy helpers (tools, SOUL, loop, participants, messages, context_build, Honcho mirror). Scripts prefer PG/memory over paths.
- **openai_processor.c**: Context builder + response status parse (identity prefix, secret references, tools). JSON only; transport in caller (librest/shaggy or other providers).
- **honcho_interface.c**: Optional Honcho handle attach; mirror policy is narrative-only by default (tool calls excluded).
- **pique_integration.c**: libpique/pqwire helpers (no sockets). SQL builders for interaction log, session upsert, embeddings, similarity search. Feed path stages SQL into get_output (`harness_pique_feed_*`) and emits PIQUE_* events. Optional `HAVE_PIQUE` + `harness_pique_submit_staged` calls `pqwire_send_query` on `config.pique_ctx` (still no network). Similarity TSV parse emits VECTOR_HIT. Secret redaction on log SQL by default.
- **harness_extra.c / harness_policy.c**: mute, multipart, history, stream, log ring; capabilities, kind SOUL, compress, Honcho builders, pique feed/redaction/similarity parse.

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
- Lua: harness.register_tool, set_loop_criterion, set_should_mirror, next_event, drain_events, wait_event, poll_until, pique_feed_*, history_compress_select, session/participant helpers
- Sibling library APIs (libpique/pqwire, librest, shaggy, etc.) consumable from Lua via FFI or bindings.

This design keeps the harness light, flexible, and true to the sibling library philosophy while enabling powerful Lua-orchestrated multi-participant AI sessions with persistent memory and vector intelligence.
