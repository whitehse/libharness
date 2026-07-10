# libharness Documentation

See AGENTS.md for entry point.
See ARCHITECTURE.md for design and module boundaries.
See TODO.md (repo root) for major remaining implementation work.
See DOMAIN.md for domain concepts (Session, Model, Context, Response, tools, SOUL, pg_vector, Honcho).
ADRs in docs/decisions/:

- 001-bootstrap.md — project bootstrap
- 002-session-model-context-and-honcho.md — multi-party session domain model and Honcho join
- 003-single-session-per-context.md — one session per harness_ctx_t
- 004-core-as-plumbing.md — pure plumbing / no sockets; sibling-aligned philosophy
- 005-testing-fuzzing-valgrind.md — ctest / fuzz / valgrind policy (shaggy ADR 003 adapted)
- 006-dialectic-multi-context-testing.md — multi-ctx buffer dialectic (shaggy ADR 004 adapted)
- 007-opaque-interfaces-and-bindings.md — opaque C + FFI (shaggy ADR 010 adapted)
- 008-lua-policy-layer.md — Lua outside core plumbing (shaggy ADR 014 adapted)
- docs/honcho-transport-notes.md — caller-owned Honcho HTTP notes
- docs/pique-transport-notes.md — caller-owned libpique SQL feed notes
