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
- docs/honcho-transport-notes.md — caller-owned Honcho HTTP notes
- docs/pique-transport-notes.md — caller-owned libpique SQL feed notes
