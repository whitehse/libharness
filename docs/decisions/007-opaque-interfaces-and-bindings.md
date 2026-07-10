# ADR 007: Opaque C Interfaces and Language-Binding Friendly API

**Date**: 2026-07-10
**Status**: Accepted
**Deciders**: Project maintainers

## Context

Hanson's *C Interfaces and Implementations* patterns and shaggy ADR 010 inform
sibling libraries. libharness already exposes an opaque `harness_ctx_t` and Lua
bindings; this ADR formalizes the standing rules so new surfaces stay FFI-safe.

## Decision

1. **Opaque contexts** — `harness_ctx_t` remains incomplete in the public header.
2. **Prefix** — all public symbols use `harness_`.
3. **Ownership** — create/destroy pairs; caller-owned config pointers are never
   freed by the library; output may use caller buffers via config.
4. **Simple types** — avoid bitfields, complex macros, and inline bodies in the
   public header that hide implementation or confuse bindgen/FFI tools.
5. **Events** — fixed-size `harness_event_t` fields (`detail[]`, ids) over
   dynamic strings inside the event struct.
6. **Lua** — bindings use lightuserdata + standard registration; `void*` is
   used at the C boundary so consumers do not need `lua.h` when linking pure C.

## Consequences

- New APIs reviewed for FFI friendliness (ADR note in AGENTS.md remains).
- Internal structs live only in `src/harness_internal.h`.

## Verification

Public headers compile as C11 with `-Wall -Wextra -Wpedantic -Werror`; tests do
not poke internal fields.
