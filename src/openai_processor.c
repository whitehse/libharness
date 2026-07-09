/* OpenAI-compatible context builder + response parser (ADR 002).
 * Builds/parses JSON only. Network transport stays in the caller.
 */
#include "harness_internal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char* msg_role_str(harness_message_role_t role) {
    switch (role) {
    case HARNESS_MSG_SYSTEM: return "system";
    case HARNESS_MSG_DEVELOPER: return "developer";
    case HARNESS_MSG_USER: return "user";
    case HARNESS_MSG_ASSISTANT: return "assistant";
    case HARNESS_MSG_TOOL: return "tool";
    default: return "user";
    }
}

/* Minimal JSON string escape into dest; returns length written (no NUL), -1 on overflow. */
static int json_escape_append(char* dest, size_t cap, size_t* used, const char* src) {
    size_t i;
    if (!dest || !used || !src) return -1;
    for (i = 0; src[i]; i++) {
        char c = src[i];
        const char* rep = NULL;
        char tmp[7];
        size_t rep_len = 0;
        size_t j;
        if (c == '\\') { rep = "\\\\"; rep_len = 2; }
        else if (c == '"') { rep = "\\\""; rep_len = 2; }
        else if (c == '\n') { rep = "\\n"; rep_len = 2; }
        else if (c == '\r') { rep = "\\r"; rep_len = 2; }
        else if (c == '\t') { rep = "\\t"; rep_len = 2; }
        else if ((unsigned char)c < 0x20) {
            snprintf(tmp, sizeof(tmp), "\\u%04x", (unsigned char)c);
            rep = tmp;
            rep_len = 6;
        }
        if (rep) {
            if (*used + rep_len >= cap) return -1;
            for (j = 0; j < rep_len; j++) dest[(*used)++] = rep[j];
        } else {
            if (*used + 1 >= cap) return -1;
            dest[(*used)++] = c;
        }
    }
    return 0;
}

static int append_raw(char* dest, size_t cap, size_t* used, const char* s) {
    size_t n;
    if (!s) return 0;
    n = strlen(s);
    if (*used + n >= cap) return -1;
    memcpy(dest + *used, s, n);
    *used += n;
    return 0;
}

static bool acting_peer_privileged(const harness_ctx_t* ctx) {
    const harness_participant_slot_t* p;
    if (!ctx || ctx->acting_peer_id[0] == '\0') return true; /* no actor → no redaction */
    p = harness_find_participant(ctx, ctx->acting_peer_id);
    if (!p) return false;
    return p->privileged;
}

int harness_openai_context_build_impl(harness_ctx_t* ctx, const harness_context_params_t* params) {
    char* buf;
    size_t cap = 64 * 1024;
    size_t used = 0;
    size_t i;
    bool prefix = params->identity_prefix;
    bool redact = params->redact_secrets && !acting_peer_privileged(ctx);
    char temp[HARNESS_MSG_CONTENT_MAX + HARNESS_PEER_ID_MAX + 16];
    char secret_ref[64];
    int secret_i = 0;

    if (!ctx || !params) return -1;

    buf = (char*)malloc(cap);
    if (!buf) return -1;

    if (append_raw(buf, cap, &used, "{") != 0) goto fail;

    if (params->model && params->model[0]) {
        if (append_raw(buf, cap, &used, "\"model\":\"") != 0) goto fail;
        if (json_escape_append(buf, cap, &used, params->model) != 0) goto fail;
        if (append_raw(buf, cap, &used, "\",") != 0) goto fail;
    }

    if (params->temperature >= 0.0) {
        char tbuf[64];
        snprintf(tbuf, sizeof(tbuf), "\"temperature\":%.4g,", params->temperature);
        if (append_raw(buf, cap, &used, tbuf) != 0) goto fail;
    }

    /* messages */
    if (append_raw(buf, cap, &used, "\"messages\":[") != 0) goto fail;

    /* SOUL as system message first when set */
    if (ctx->soul[0] != '\0') {
        if (append_raw(buf, cap, &used, "{\"role\":\"system\",\"content\":\"") != 0) goto fail;
        if (json_escape_append(buf, cap, &used, ctx->soul) != 0) goto fail;
        if (append_raw(buf, cap, &used, "\"}") != 0) goto fail;
        if (ctx->message_count > 0 || (params->include_tools && ctx->tool_count > 0)) {
            /* comma after soul if more content follows messages — handled below */
        }
        if (ctx->message_count > 0) {
            if (append_raw(buf, cap, &used, ",") != 0) goto fail;
        }
    }

    for (i = 0; i < ctx->message_count; i++) {
        const harness_message_slot_t* msg = &ctx->messages[i];
        const char* body = msg->content;

        if (i > 0) {
            if (append_raw(buf, cap, &used, ",") != 0) goto fail;
        }

        if (append_raw(buf, cap, &used, "{\"role\":\"") != 0) goto fail;
        if (append_raw(buf, cap, &used, msg_role_str(msg->role)) != 0) goto fail;
        if (append_raw(buf, cap, &used, "\"") != 0) goto fail;

        if (msg->role == HARNESS_MSG_TOOL && msg->tool_call_id[0]) {
            if (append_raw(buf, cap, &used, ",\"tool_call_id\":\"") != 0) goto fail;
            if (json_escape_append(buf, cap, &used, msg->tool_call_id) != 0) goto fail;
            if (append_raw(buf, cap, &used, "\"") != 0) goto fail;
        }

        if (msg->is_secret && redact) {
            secret_i++;
            snprintf(secret_ref, sizeof(secret_ref), "[secret:ref_%d]", secret_i);
            body = secret_ref;
            harness_emit(ctx, HARNESS_EVENT_SECRET_REFERENCED, msg->peer_id, NULL, secret_i, i);
        }

        temp[0] = '\0';
        if (prefix && msg->peer_id[0] != '\0' &&
            msg->role != HARNESS_MSG_SYSTEM && msg->role != HARNESS_MSG_DEVELOPER &&
            msg->role != HARNESS_MSG_TOOL) {
            snprintf(temp, sizeof(temp), "[%s]: %s", msg->peer_id, body);
            body = temp;
        }

        if (append_raw(buf, cap, &used, ",\"content\":\"") != 0) goto fail;
        if (json_escape_append(buf, cap, &used, body) != 0) goto fail;
        if (append_raw(buf, cap, &used, "\"}") != 0) goto fail;
    }

    if (append_raw(buf, cap, &used, "]") != 0) goto fail;

    if (params->include_tools && ctx->tool_count > 0) {
        if (append_raw(buf, cap, &used, ",\"tools\":[") != 0) goto fail;
        for (i = 0; i < ctx->tool_count; i++) {
            if (i > 0 && append_raw(buf, cap, &used, ",") != 0) goto fail;
            if (append_raw(buf, cap, &used, ctx->tools[i].json) != 0) goto fail;
        }
        if (append_raw(buf, cap, &used, "]") != 0) goto fail;
    }

    if (append_raw(buf, cap, &used, "}") != 0) goto fail;
    buf[used] = '\0';

    if (harness_set_output(ctx, buf, used) != 0) goto fail;
    free(buf);
    harness_emit(ctx, HARNESS_EVENT_CONTEXT_READY, ctx->acting_peer_id, NULL, 0, used);
    ctx->state = HARNESS_STATE_READY;
    return 0;

fail:
    free(buf);
    harness_emit(ctx, HARNESS_EVENT_ERROR, NULL, NULL, -1, 0);
    ctx->state = HARNESS_STATE_ERROR;
    return -1;
}

int harness_openai_response_parse_impl(harness_ctx_t* ctx, const uint8_t* data, size_t len) {
    /* Lightweight stub detection: look for status/requires_action/tool_calls substrings.
     * Full JSON parse (libjsparse) is a TODO item.
     */
    char* tmp;
    int requires = 0;
    size_t tool_calls = 0;
    const char* p;
    const char* end;

    if (!ctx || !data || len == 0) return -1;

    tmp = (char*)malloc(len + 1);
    if (!tmp) return -1;
    memcpy(tmp, data, len);
    tmp[len] = '\0';

    if (strstr(tmp, "requires_action") != NULL || strstr(tmp, "\"tool_calls\"") != NULL ||
        strstr(tmp, "function_call") != NULL) {
        requires = 1;
    }

    /* crude count of "call_" ids */
    p = tmp;
    end = tmp + len;
    while (p < end) {
        const char* hit = strstr(p, "call_");
        if (!hit) break;
        tool_calls++;
        p = hit + 5;
    }

    ctx->last_tool_call_count = requires ? (tool_calls > 0 ? tool_calls : 1) : 0;
    if (requires) {
        ctx->last_response_status = HARNESS_RESPONSE_REQUIRES_ACTION;
        ctx->state = HARNESS_STATE_TOOL_CALL;
        harness_emit(ctx, HARNESS_EVENT_RESPONSE_REQUIRES_ACTION, ctx->acting_peer_id, NULL,
                     0, ctx->last_tool_call_count);
    } else {
        ctx->last_response_status = HARNESS_RESPONSE_COMPLETED;
        ctx->state = HARNESS_STATE_READY;
        harness_emit(ctx, HARNESS_EVENT_RESPONSE_COMPLETED, ctx->acting_peer_id, NULL, 0, 0);
    }

    /* Keep raw response available on output for caller inspection */
    (void)harness_set_output(ctx, data, len);
    free(tmp);
    return 0;
}
