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

int harness_honcho_mirror_message(harness_ctx_t* ctx,
                                  const char* peer_id,
                                  const char* content,
                                  const char* metadata_json) {
    (void)metadata_json;
    if (!ctx || !peer_id || !content) return -1;
    if (!harness_honcho_should_mirror(ctx, content))
        return -1;
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
    if (!harness_honcho_should_mirror(ctx, content))
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


bool harness_honcho_should_mirror(const harness_ctx_t* ctx, const char* content) {
    int lua_bool = -1;
    if (!content) return false;
    if (ctx && harness_lua_eval_should_mirror((harness_ctx_t*)ctx, content, &lua_bool) == 0)
        return lua_bool != 0;
    if (ctx && ctx->config.mirror_tool_calls) return true;
    if (strstr(content, "\"tool_calls\"") != NULL) return false;
    if (strstr(content, "\"function_call\"") != NULL) return false;
    if (strstr(content, "\"type\":\"function_call\"") != NULL) return false;
    if (strstr(content, "call_id") != NULL && strstr(content, "arguments") != NULL)
        return false;
    return true;
}

int harness_honcho_metadata_chat(char* buf, size_t cap, size_t* out_len) {
    const char* s = "{\"type\":\"chat_message\"}";
    size_t n;
    if (!buf || cap == 0) return -1;
    n = strlen(s);
    if (n >= cap) return -1;
    memcpy(buf, s, n + 1);
    if (out_len) *out_len = n;
    return 0;
}

int harness_honcho_metadata_agent(char* buf, size_t cap, size_t* out_len, const char* model) {
    size_t used = 0;
    if (!buf || cap == 0) return -1;
    if (harness_json_append_raw(buf, cap, &used, "{\"type\":\"agent_response\"") != 0)
        return -1;
    if (model && model[0]) {
        if (harness_json_append_raw(buf, cap, &used, ",\"model\":\"") != 0) return -1;
        if (harness_json_escape_append(buf, cap, &used, model) != 0) return -1;
        if (harness_json_append_raw(buf, cap, &used, "\"") != 0) return -1;
    }
    if (harness_json_append_raw(buf, cap, &used, "}") != 0) return -1;
    buf[used] = '\0';
    if (out_len) *out_len = used;
    return 0;
}

int harness_honcho_parse_response(harness_ctx_t* ctx, const uint8_t* data, size_t len) {
    char* tmp;
    const char* p;
    size_t count = 0;
    if (!ctx || !data || len == 0) return -1;
    tmp = (char*)malloc(len + 1);
    if (!tmp) return -1;
    memcpy(tmp, data, len);
    tmp[len] = '\0';
    p = tmp;
    while ((p = strstr(p, "\"peer_id\"")) != NULL) {
        char peer[HARNESS_PEER_ID_CAP];
        const char* q = strchr(p, ':');
        peer[0] = '\0';
        if (q) {
            q = strchr(q, '"');
            if (q) {
                const char* e;
                size_t n;
                q++;
                e = strchr(q, '"');
                if (e) {
                    n = (size_t)(e - q);
                    if (n >= sizeof(peer)) n = sizeof(peer) - 1;
                    memcpy(peer, q, n);
                    peer[n] = '\0';
                    harness_emit(ctx, HARNESS_EVENT_HONCHO_RESPONSE_PARSED, peer, NULL, 0, count);
                    count++;
                    p = e + 1;
                    continue;
                }
            }
        }
        p += 8;
    }
    free(tmp);
    if (count == 0)
        harness_emit(ctx, HARNESS_EVENT_HONCHO_RESPONSE_PARSED, NULL, NULL, 0, 0);
    return 0;
}

int harness_honcho_feed_peer_card(harness_ctx_t* ctx, const char* peer_id) {
    char buf[1024];
    size_t n = 0;
    if (!ctx || !peer_id) return -1;
    if (harness_honcho_build_peer_card_request(ctx, peer_id, buf, sizeof(buf), &n) != 0)
        return -1;
    if (harness_set_output(ctx, buf, n) != 0) return -1;
    harness_emit_ex(ctx, HARNESS_EVENT_HONCHO_REQUEST_READY, peer_id, NULL, 0, n,
                    "peer_card");
    return 0;
}

int harness_honcho_feed_conclude(harness_ctx_t* ctx,
                                 const char* peer_id,
                                 const char* conclusion) {
    char buf[2048];
    size_t n = 0;
    if (!ctx || !peer_id || !conclusion) return -1;
    if (harness_honcho_build_conclude_request(ctx, peer_id, conclusion, buf,
                                              sizeof(buf), &n) != 0)
        return -1;
    if (harness_set_output(ctx, buf, n) != 0) return -1;
    harness_emit_ex(ctx, HARNESS_EVENT_HONCHO_REQUEST_READY, peer_id, NULL, 0, n,
                    "conclude");
    return 0;
}
