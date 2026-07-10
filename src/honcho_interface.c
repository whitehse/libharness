/* Honcho workspace/peer/session/message surface (ADR 002).
 * Optional at deploy; peer ids remain first-class without a live Honcho.
 * Tool calls are not mirrored by default.
 */
#include "harness_internal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int harness_honcho_attach(harness_ctx_t* ctx, void* honcho_handle) {
    if (!ctx) return -1;
    ctx->honcho = honcho_handle;
    return 0;
}

static bool looks_like_tool_payload(const char* content) {
    if (!content) return false;
    if (strstr(content, "\"tool_calls\"") != NULL) return true;
    if (strstr(content, "\"function_call\"") != NULL) return true;
    if (strstr(content, "\"type\":\"function_call\"") != NULL) return true;
    if (strstr(content, "call_id") != NULL && strstr(content, "arguments") != NULL)
        return true;
    return false;
}

int harness_honcho_mirror_message(harness_ctx_t* ctx,
                                  const char* peer_id,
                                  const char* content,
                                  const char* metadata_json) {
    (void)metadata_json;
    if (!ctx || !peer_id || !content) return -1;
    if (!ctx->config.mirror_tool_calls && looks_like_tool_payload(content))
        return -1;
    /* Secrets: refuse raw secret-looking values if they match a secret message ref
     * is policy; simple refuse if content contains unredacted password patterns is out of scope.
     * Narrative mirror only. */
    harness_emit(ctx, HARNESS_EVENT_HONCHO_MIRRORED, peer_id, NULL, 0, 0);
    return 0;
}

int harness_honcho_build_messages_request(harness_ctx_t* ctx,
                                          const char* peer_id,
                                          const char* content,
                                          const char* metadata_json) {
    char* buf;
    size_t cap = 8192;
    size_t used = 0;

    if (!ctx || !peer_id || !content) return -1;
    if (!ctx->config.mirror_tool_calls && looks_like_tool_payload(content))
        return -1;

    buf = (char*)malloc(cap);
    if (!buf) return -1;

    if (harness_json_append_raw(buf, cap, &used, "{\"messages\":[{\"peer_id\":\"") != 0)
        goto fail;
    if (harness_json_escape_append(buf, cap, &used, peer_id) != 0) goto fail;
    if (harness_json_append_raw(buf, cap, &used, "\",\"content\":\"") != 0) goto fail;
    if (harness_json_escape_append(buf, cap, &used, content) != 0) goto fail;
    if (harness_json_append_raw(buf, cap, &used, "\"") != 0) goto fail;
    if (metadata_json && metadata_json[0]) {
        if (harness_json_append_raw(buf, cap, &used, ",\"metadata\":") != 0) goto fail;
        if (harness_json_append_raw(buf, cap, &used, metadata_json) != 0) goto fail;
    }
    if (ctx->session_id[0]) {
        if (harness_json_append_raw(buf, cap, &used, ",\"session_id\":\"") != 0) goto fail;
        if (harness_json_escape_append(buf, cap, &used, ctx->session_id) != 0) goto fail;
        if (harness_json_append_raw(buf, cap, &used, "\"") != 0) goto fail;
    }
    if (ctx->workspace_id[0]) {
        if (harness_json_append_raw(buf, cap, &used, ",\"workspace_id\":\"") != 0) goto fail;
        if (harness_json_escape_append(buf, cap, &used, ctx->workspace_id) != 0) goto fail;
        if (harness_json_append_raw(buf, cap, &used, "\"") != 0) goto fail;
    }
    if (harness_json_append_raw(buf, cap, &used, "}]}") != 0) goto fail;
    buf[used] = '\0';

    if (harness_set_output(ctx, buf, used) != 0) goto fail;
    free(buf);
    harness_emit(ctx, HARNESS_EVENT_HONCHO_REQUEST_READY, peer_id, NULL, 0, used);
    return 0;

fail:
    free(buf);
    return -1;
}
