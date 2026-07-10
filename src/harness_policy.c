/* Policy helpers: capabilities, kind SOUL, history compress, pique SQL builders. */
#include "harness_internal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

bool harness_peer_can_see_secrets(const harness_ctx_t* ctx, const char* peer_id) {
    const harness_participant_slot_t* p;
    if (!ctx || !peer_id || !peer_id[0]) return true;
    p = harness_find_participant(ctx, peer_id);
    if (!p) return false;
    if (p->capabilities & HARNESS_CAP_SEE_SECRETS) return true;
    return p->privileged;
}

int harness_participant_set_capabilities(harness_ctx_t* ctx,
                                         const char* peer_id,
                                         uint32_t capabilities) {
    harness_participant_slot_t* slot = harness_find_participant_mut(ctx, peer_id);
    if (!slot) return -1;
    slot->capabilities = capabilities;
    slot->privileged = (capabilities & HARNESS_CAP_SEE_SECRETS) != 0;
    return 0;
}

uint32_t harness_participant_get_capabilities(const harness_ctx_t* ctx,
                                              const char* peer_id) {
    const harness_participant_slot_t* slot = harness_find_participant(ctx, peer_id);
    if (!slot) return HARNESS_CAP_NONE;
    if (slot->capabilities != 0) return slot->capabilities;
    return slot->privileged ? HARNESS_CAP_SEE_SECRETS : HARNESS_CAP_NONE;
}

int harness_soul_set_for_kind(harness_ctx_t* ctx,
                              harness_participant_kind_t kind,
                              const char* soul_text) {
    if (!ctx || !soul_text) return -1;
    if ((int)kind < 0 || (int)kind > 2) return -1;
    harness_copy_id(ctx->soul_by_kind[kind], sizeof(ctx->soul_by_kind[kind]), soul_text);
    harness_emit(ctx, HARNESS_EVENT_SOUL_SET, NULL, NULL, (int)kind, 0);
    return 0;
}

const char* harness_soul_get_for_kind(const harness_ctx_t* ctx,
                                      harness_participant_kind_t kind) {
    if (!ctx || (int)kind < 0 || (int)kind > 2) return NULL;
    return ctx->soul_by_kind[kind];
}

int harness_history_compress(harness_ctx_t* ctx, size_t keep_last) {
    size_t dropped = 0;
    if (!ctx) return -1;
    if (keep_last == 0) keep_last = 1;
    while (ctx->message_count > keep_last) {
        size_t i;
        size_t drop = (size_t)-1;
        /* Prefer dropping oldest non-tool, non-system/developer messages */
        for (i = 0; i < ctx->message_count; i++) {
            harness_message_role_t r = ctx->messages[i].role;
            if (r == HARNESS_MSG_TOOL || r == HARNESS_MSG_SYSTEM || r == HARNESS_MSG_DEVELOPER)
                continue;
            drop = i;
            break;
        }
        if (drop == (size_t)-1) {
            /* fall back to drop oldest anything but last keep_last */
            drop = 0;
        }
        if (drop + 1 < ctx->message_count) {
            memmove(&ctx->messages[drop], &ctx->messages[drop + 1],
                    (ctx->message_count - drop - 1) * sizeof(ctx->messages[0]));
        }
        ctx->message_count--;
        memset(&ctx->messages[ctx->message_count], 0, sizeof(ctx->messages[0]));
        dropped++;
    }
    harness_emit(ctx, HARNESS_EVENT_HISTORY_COMPRESSED, NULL, NULL, (int)dropped,
                 ctx->message_count);
    harness_emit(ctx, HARNESS_EVENT_VECTOR_CLASSIFIED, NULL, NULL, (int)dropped,
                 keep_last);
    return 0;
}

static int sql_escape_append(char* dest, size_t cap, size_t* used, const char* src) {
    size_t i;
    if (!src) src = "";
    for (i = 0; src[i]; i++) {
        if (src[i] == '\'') {
            if (*used + 2 >= cap) return -1;
            dest[(*used)++] = '\'';
            dest[(*used)++] = '\'';
        } else {
            if (*used + 1 >= cap) return -1;
            dest[(*used)++] = src[i];
        }
    }
    return 0;
}

int harness_pique_build_log_insert(const harness_ctx_t* ctx,
                                   const char* model,
                                   const char* prompt,
                                   const char* response,
                                   char* buf,
                                   size_t cap,
                                   size_t* out_len) {
    size_t used = 0;
    char num[32];
    if (!ctx || !buf || cap == 0 || !model) return -1;
    if (harness_json_append_raw(buf, cap, &used,
            "INSERT INTO harness_interactions "
            "(session_id, workspace_id, acting_peer_id, model, prompt, response, "
            "prompt_tokens, completion_tokens, total_tokens) VALUES ('") != 0)
        return -1;
    if (sql_escape_append(buf, cap, &used, ctx->session_id) != 0) return -1;
    if (harness_json_append_raw(buf, cap, &used, "','") != 0) return -1;
    if (sql_escape_append(buf, cap, &used, ctx->workspace_id) != 0) return -1;
    if (harness_json_append_raw(buf, cap, &used, "','") != 0) return -1;
    if (sql_escape_append(buf, cap, &used, ctx->acting_peer_id) != 0) return -1;
    if (harness_json_append_raw(buf, cap, &used, "','") != 0) return -1;
    if (sql_escape_append(buf, cap, &used, model) != 0) return -1;
    if (harness_json_append_raw(buf, cap, &used, "','") != 0) return -1;
    if (sql_escape_append(buf, cap, &used, prompt ? prompt : "") != 0) return -1;
    if (harness_json_append_raw(buf, cap, &used, "','") != 0) return -1;
    if (sql_escape_append(buf, cap, &used, response ? response : "") != 0) return -1;
    snprintf(num, sizeof(num), "',%u,%u,%u);",
             (unsigned)ctx->last_usage.prompt_tokens,
             (unsigned)ctx->last_usage.completion_tokens,
             (unsigned)ctx->last_usage.total_tokens);
    if (harness_json_append_raw(buf, cap, &used, num) != 0) return -1;
    if (used >= cap) return -1;
    buf[used] = '\0';
    if (out_len) *out_len = used;
    return 0;
}

int harness_pique_build_session_upsert(const harness_ctx_t* ctx,
                                       char* buf,
                                       size_t cap,
                                       size_t* out_len) {
    size_t used = 0;
    if (!ctx || !buf || cap == 0) return -1;
    if (harness_json_append_raw(buf, cap, &used,
            "INSERT INTO harness_sessions (workspace_id, session_id, retired) VALUES ('") != 0)
        return -1;
    if (sql_escape_append(buf, cap, &used, ctx->workspace_id) != 0) return -1;
    if (harness_json_append_raw(buf, cap, &used, "','") != 0) return -1;
    if (sql_escape_append(buf, cap, &used, ctx->session_id) != 0) return -1;
    if (harness_json_append_raw(buf, cap, &used,
            ctx->session_retired ? "',TRUE) ON CONFLICT (session_id) DO UPDATE SET retired=EXCLUDED.retired;"
                                 : "',FALSE) ON CONFLICT (session_id) DO UPDATE SET retired=EXCLUDED.retired;") != 0)
        return -1;
    if (used >= cap) return -1;
    buf[used] = '\0';
    if (out_len) *out_len = used;
    return 0;
}

int harness_honcho_build_peer_card_request(harness_ctx_t* ctx,
                                           const char* peer_id,
                                           char* buf,
                                           size_t cap,
                                           size_t* out_len) {
    size_t used = 0;
    if (!ctx || !peer_id || !buf || cap == 0) return -1;
    if (harness_json_append_raw(buf, cap, &used, "{\"peer_id\":\"") != 0) return -1;
    if (harness_json_escape_append(buf, cap, &used, peer_id) != 0) return -1;
    if (harness_json_append_raw(buf, cap, &used, "\",\"op\":\"get_peer_card\"") != 0) return -1;
    if (ctx->workspace_id[0]) {
        if (harness_json_append_raw(buf, cap, &used, ",\"workspace_id\":\"") != 0) return -1;
        if (harness_json_escape_append(buf, cap, &used, ctx->workspace_id) != 0) return -1;
        if (harness_json_append_raw(buf, cap, &used, "\"") != 0) return -1;
    }
    if (harness_json_append_raw(buf, cap, &used, "}") != 0) return -1;
    buf[used] = '\0';
    if (out_len) *out_len = used;
    return 0;
}

int harness_honcho_build_conclude_request(harness_ctx_t* ctx,
                                          const char* peer_id,
                                          const char* conclusion,
                                          char* buf,
                                          size_t cap,
                                          size_t* out_len) {
    size_t used = 0;
    if (!ctx || !peer_id || !conclusion || !buf || cap == 0) return -1;
    if (harness_json_append_raw(buf, cap, &used, "{\"peer_id\":\"") != 0) return -1;
    if (harness_json_escape_append(buf, cap, &used, peer_id) != 0) return -1;
    if (harness_json_append_raw(buf, cap, &used, "\",\"op\":\"conclude\",\"conclusion\":\"") != 0)
        return -1;
    if (harness_json_escape_append(buf, cap, &used, conclusion) != 0) return -1;
    if (harness_json_append_raw(buf, cap, &used, "\"") != 0) return -1;
    if (ctx->workspace_id[0]) {
        if (harness_json_append_raw(buf, cap, &used, ",\"workspace_id\":\"") != 0) return -1;
        if (harness_json_escape_append(buf, cap, &used, ctx->workspace_id) != 0) return -1;
        if (harness_json_append_raw(buf, cap, &used, "\"") != 0) return -1;
    }
    if (harness_json_append_raw(buf, cap, &used, "}") != 0) return -1;
    buf[used] = '\0';
    if (out_len) *out_len = used;
    return 0;
}
