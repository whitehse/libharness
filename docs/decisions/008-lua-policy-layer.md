# ADR 008: Lua Policy Layer Outside Core Plumbing

**Date**: 2026-07-10
**Status**: Accepted
**Deciders**: Project maintainers

## Context

Shaggy ADR 014 argues for Lua policy **without** embedding Lua into protocol
core. libharness already places tool bodies, loop criteria, and mirror policy
in Lua when `HAVE_LUA` — this ADR states the non-negotiable split.

## Decision

1. **C core is plumbing** — context build, response normalize, history, feed
   path staging, event emission. No Lua VM required to link a pure C consumer.
2. **Lua is optional policy** — `harness_lua_init(ctx, void* L)` attaches an
   application-owned `lua_State`; the library does not create files, sockets,
   or threads for Lua.
3. **Yield at event boundaries** — helpers (`next_event`, `wait_event`,
   `poll_until`) return nil / `"would_yield"`; coroutines are scheduled by the
   application event loop, never by blocking inside libharness.
4. **Tools and criteria** — registered Lua functions run only when the app
   invokes policy entry points (`harness_tool_policy_invoke`,
   `harness_should_loop`); core never auto-executes tools.
5. **Sandboxes are the app’s job** — I/O available to Lua depends on the host
   (FFI to libpique, network, etc.); the library documents preference for
   PG-backed script sources over bare `lua_init_script` paths for production.

## Consequences

- New policy hooks should prefer registration over hard-coded C branches.
- `HAVE_LUA=0` builds keep stubs that return errors rather than degrated core
  state machine semantics.

## Verification

`tests/test_policy_pique.c` exercises register_tool / mirror / loop /
event helpers with an external `lua_State`.
