#include "harness.h"
#include <string.h>

/* OpenAI API compatible processor.
   Prepares chat completions style requests (with tools, personality/system prompt).
   Handles tool call responses, streaming stubs, etc.
   Network transport is in caller (librest or shaggy).
   This module only builds/validates the JSON payloads and parses responses into events.
*/

int harness_openai_build_request(harness_ctx_t* ctx, const char* model, const char* system_personality,
                                 const char** messages, size_t msg_count,
                                 const char** tool_defs, size_t tool_count,
                                 uint8_t* out_buf, size_t max_out) {
    if (!ctx || !out_buf) return -1;
    /* Stub: build minimal OpenAI chat.completions JSON */
    const char* stub = "{\"model\":\"gpt-4o\",\"messages\":[{\"role\":\"system\",\"content\":\"stub personality\"}],\"tools\":[]}";
    size_t len = strlen(stub);
    if (len > max_out) len = max_out;
    memcpy(out_buf, stub, len);
    return (int)len;
}

int harness_openai_parse_response(harness_ctx_t* ctx, const uint8_t* response, size_t len) {
    if (!ctx || !response) return -1;
    /* Stub: parse tool_calls, content, finish_reason; emit events; log via pique */
    harness_lua_log_interaction(ctx, "gpt-4o", "stub-prompt", (const char*)response);
    return 0;
}