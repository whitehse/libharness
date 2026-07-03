# ADR 001: Bootstrap libharness as C/Lua AI Harness

## Status
Accepted

## Context
Need a lightweight C + Lua library to orchestrate AI agents using sibling libs (pique for PG/pg_vector, rest/shaggy for OpenAI, Honcho for memory). Must follow pure plumbing philosophy: no syscalls/callbacks in core, Lua for behavior and tools, dynamic everything, PG for all persistence/logging/vectors.

## Decision
Create agent-ready CMake project with:
- harness.h public API + Lua bindings
- State machine in C
- Stubs for OpenAI processor, Honcho, pique integration
- Smoke test
- Full docs scaffold (AGENTS, ARCHITECTURE, DOMAIN, ADR)

Lua 5.4, strict warnings, event-driven.

## Consequences
- Foundation for iterative addition of real pg_vector queries, OpenAI JSON handling, Lua tool execution, extension points.
- Build requires Lua dev headers and (optionally) libpique.
- Network and Honcho details remain caller or context provided.