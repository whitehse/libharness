# DOMAIN.md — AI Harness Concepts

## Harness Characteristics (Lua-exposed)
- Dynamic tool list (enumerate_tools, register_tool in Lua)
- Dynamic personality (set_personality, stored/retrieved via pg_vector + Honcho)
- OpenAI compatible processor (build_request, parse_response with tool_calls, system prompt injection)
- Loop control (should_loop(criteria) — criteria can be Lua expression or state from PG)
- Deterministic Lua behavior scripts
- Lua tools that call sibling APIs (pique, rest, etc.)
- Honcho memory interface
- PG logging + pg_vector classification for token efficiency

## Key Workflows
1. Initialize harness with pique_ctx + honcho_ctx + Lua state
2. Load personality from PG vector search
3. Enumerate tools (Lua + registered)
4. Build OpenAI request (personality + tools)
5. Caller sends via network, feeds response back
6. Parse, possibly call tools (Lua or C), log interaction to PG
7. Decide loop or exit based on criteria
8. Classify state with vectors to compress context

All model calls logged. Personalities and memory in PG with embeddings.