# ARCHITECTURE.md — libharness

## Module Boundaries
- **harness.c**: Core state machine (explicit states: INIT, READY, PROCESSING_OPENAI, TOOL_CALL, LOOPING, LOGGING, VECTOR_OP, ERROR). Pure, no syscalls. Event queue, extension registry.
- **lua_bindings.c**: Lua C API integration. Exposes harness characteristics to Lua scripts. Lua defines deterministic behavior, tools, loop criteria. Scripts loaded from PG or memory (no local files preferred).
- **openai_processor.c**: OpenAI chat completions compatible request builder and response parser. Dynamic tools + personality injection. JSON payloads only; transport in caller (librest/shaggy).
- **honcho_interface.c**: Abstraction over Honcho peer memory system for long-term facts, conclusions, session context. Personalities and memory stored/retrieved here.
- **pique_integration.c**: libpique (PostgreSQL wire protocol) integration. Logs every model interaction. Uses pg_vector for:
  - Token-reducing classification of state/data
  - Personality embeddings and similarity search
  - Local memory vectors
  All persistence via PG; local files minimized.

## Invariants
- No sockets, files, malloc in hot paths unless caller-controlled.
- All I/O (network to OpenAI, PG wire via pique, Honcho) owned by calling application or explicit context.
- Lua is the policy layer; C is the plumbing (ADR 006 style).
- Extensions (C functions or Lua modules) registered at well-defined points without polluting core state machine.
- Event-driven preferred: feed_input → next_event → get_output.

## Deliberate Absences
- No built-in HTTP client or OpenAI SDK (network in caller).
- No direct file I/O for scripts or logs (PG preferred).
- No hard-coded models or personalities (dynamic via Lua + PG vectors).
- No callbacks; explicit events only.

## Extension Points
- harness_register_extension (C)
- Lua: harness.register_tool, harness.register_loop_criterion, etc. (to be implemented)
- Sibling library APIs (libpique, librest, shaggy, libjsparse for JSON, etc.) consumable from Lua via FFI or bindings.

This design keeps the harness light, flexible, and true to the sibling library philosophy while enabling powerful Lua-orchestrated AI loops with persistent memory and vector intelligence.