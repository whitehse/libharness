/* Additional ADR 002 surfaces: mute, multipart, history, stream, log ring.
 * Keeps harness.c from growing further; still pure plumbing, no I/O.
 */
#include "harness_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

const char* harness_msg_role_name(harness_message_role_t role) {
    switch (role) {
    case HARNESS_MSG_SYSTEM: return "system";
    case HARNESS_MSG_DEVELOPER: return "developer";
    case HARNESS_MSG_USER: return "user";
    case HARNESS_MSG_ASSISTANT: return "assistant";
    case HARNESS_MSG_TOOL: return "tool";
    default: return "user";
    }
}

int harness_msg_role_from_name(const char* name, harness_message_role_t* out) {
    if (!name || !out) return -1;
    if (strcmp(name, "system") == 0) { *out = HARNESS_MSG_SYSTEM; return 0; }
    if (strcmp(name, "developer") == 0) { *out = HARNESS_MSG_DEVELOPER; return 0; }
    if (strcmp(name, "user") == 0) { *out = HARNESS_MSG_USER; return 0; }
    if (strcmp(name, "assistant") == 0) { *out = HARNESS_MSG_ASSISTANT; return 0; }
    if (strcmp(name, "tool") == 0) { *out = HARNESS_MSG_TOOL; return 0; }
    return -1;
}

int harness_participant_set_muted(harness_ctx_t* ctx,
                                  const char* peer_id,
                                  bool muted) {
    harness_participant_slot_t* slot = harness_find_participant_mut(ctx, peer_id);
    if (!slot) return -1;
    slot->muted = muted;
    harness_emit(ctx, HARNESS_EVENT_PARTICIPANT_MUTED, peer_id, NULL, muted ? 1 : 0, 0);
    return 0;
}

bool harness_participant_is_muted(const harness_ctx_t* ctx, const char* peer_id) {
    const harness_participant_slot_t* slot = harness_find_participant(ctx, peer_id);
    return slot ? slot->muted : false;
}

int harness_message_append_parts(harness_ctx_t* ctx,
                                 const char* peer_id,
                                 harness_message_role_t role,
                                 const harness_content_part_t* parts,
                                 size_t part_count,
                                 bool is_secret) {
    char json[HARNESS_MSG_CONTENT_MAX];
    size_t used = 0;
    size_t i;
    harness_message_slot_t* slot;

    if (!ctx || !parts || part_count == 0) return -1;
    if (ctx->session_retired) return -1;
    if (harness_ensure_message_slot(ctx) != 0) return -1;

    if (harness_json_append_raw(json, sizeof(json), &used, "[") != 0) return -1;
    for (i = 0; i < part_count; i++) {
        const char* t = parts[i].type ? parts[i].type : "text";
        if (i > 0 && harness_json_append_raw(json, sizeof(json), &used, ",") != 0) return -1;
        if (harness_json_append_raw(json, sizeof(json), &used, "{\"type\":\"") != 0) return -1;
        if (harness_json_escape_append(json, sizeof(json), &used, t) != 0) return -1;
        if (harness_json_append_raw(json, sizeof(json), &used, "\"") != 0) return -1;
        if (strcmp(t, "image_url") == 0 && parts[i].image_url) {
            if (harness_json_append_raw(json, sizeof(json), &used,
                    ",\"image_url\":{\"url\":\"") != 0) return -1;
            if (harness_json_escape_append(json, sizeof(json), &used, parts[i].image_url) != 0)
                return -1;
            if (harness_json_append_raw(json, sizeof(json), &used, "\"}") != 0) return -1;
        } else if (strcmp(t, "refusal") == 0) {
            if (harness_json_append_raw(json, sizeof(json), &used, ",\"refusal\":\"") != 0)
                return -1;
            if (harness_json_escape_append(json, sizeof(json), &used,
                    parts[i].text ? parts[i].text : "") != 0) return -1;
            if (harness_json_append_raw(json, sizeof(json), &used, "\"") != 0) return -1;
        } else {
            if (harness_json_append_raw(json, sizeof(json), &used, ",\"text\":\"") != 0)
                return -1;
            if (harness_json_escape_append(json, sizeof(json), &used,
                    parts[i].text ? parts[i].text : "") != 0) return -1;
            if (harness_json_append_raw(json, sizeof(json), &used, "\"") != 0) return -1;
        }
        if (harness_json_append_raw(json, sizeof(json), &used, "}") != 0) return -1;
    }
    if (harness_json_append_raw(json, sizeof(json), &used, "]") != 0) return -1;
    json[used] = '\0';

    slot = &ctx->messages[ctx->message_count];
    memset(slot, 0, sizeof(*slot));
    harness_copy_id(slot->peer_id, sizeof(slot->peer_id), peer_id ? peer_id : "");
    slot->role = role;
    harness_copy_id(slot->content, sizeof(slot->content), json);
    slot->content_is_parts = true;
    slot->is_secret = is_secret;
    slot->in_use = true;
    if (is_secret) {
        ctx->secret_seq++;
        slot->secret_ref_id = ctx->secret_seq;
        harness_emit(ctx, HARNESS_EVENT_SECRET_REFERENCED, peer_id, NULL,
                     (int)slot->secret_ref_id, ctx->message_count);
    }
    ctx->message_count++;
    harness_emit(ctx, HARNESS_EVENT_MESSAGE_APPENDED, peer_id, NULL, (int)role,
                 ctx->message_count - 1);
    return 0;
}

int harness_history_export_json(const harness_ctx_t* ctx,
                                char* buf,
                                size_t cap,
                                size_t* out_len) {
    size_t used = 0;
    size_t i, t;
    if (!ctx || !buf || cap == 0) return -1;
    if (harness_json_append_raw(buf, cap, &used, "{\"messages\":[") != 0) return -1;
    for (i = 0; i < ctx->message_count; i++) {
        const harness_message_slot_t* m = &ctx->messages[i];
        if (i > 0 && harness_json_append_raw(buf, cap, &used, ",") != 0) return -1;
        if (harness_json_append_raw(buf, cap, &used, "{\"peer_id\":\"") != 0) return -1;
        if (harness_json_escape_append(buf, cap, &used, m->peer_id) != 0) return -1;
        if (harness_json_append_raw(buf, cap, &used, "\",\"role\":\"") != 0) return -1;
        if (harness_json_escape_append(buf, cap, &used, harness_msg_role_name(m->role)) != 0)
            return -1;
        if (harness_json_append_raw(buf, cap, &used, "\"") != 0) return -1;
        if (m->content_is_parts) {
            if (harness_json_append_raw(buf, cap, &used, ",\"content\":") != 0) return -1;
            if (harness_json_append_raw(buf, cap, &used, m->content) != 0) return -1;
        } else {
            if (harness_json_append_raw(buf, cap, &used, ",\"content\":\"") != 0) return -1;
            if (harness_json_escape_append(buf, cap, &used, m->content) != 0) return -1;
            if (harness_json_append_raw(buf, cap, &used, "\"") != 0) return -1;
        }
        if (m->tool_call_id[0]) {
            if (harness_json_append_raw(buf, cap, &used, ",\"tool_call_id\":\"") != 0)
                return -1;
            if (harness_json_escape_append(buf, cap, &used, m->tool_call_id) != 0) return -1;
            if (harness_json_append_raw(buf, cap, &used, "\"") != 0) return -1;
        }
        if (m->is_secret) {
            char nbuf[64];
            snprintf(nbuf, sizeof(nbuf), ",\"is_secret\":true,\"secret_ref\":%u",
                     (unsigned)m->secret_ref_id);
            if (harness_json_append_raw(buf, cap, &used, nbuf) != 0) return -1;
        }
        if (m->tool_call_count > 0) {
            if (harness_json_append_raw(buf, cap, &used, ",\"tool_calls\":[") != 0) return -1;
            for (t = 0; t < m->tool_call_count; t++) {
                const harness_embedded_tool_call_t* tc = &m->tool_calls[t];
                if (t > 0 && harness_json_append_raw(buf, cap, &used, ",") != 0) return -1;
                if (harness_json_append_raw(buf, cap, &used, "{\"id\":\"") != 0) return -1;
                if (harness_json_escape_append(buf, cap, &used, tc->id) != 0) return -1;
                if (harness_json_append_raw(buf, cap, &used, "\",\"name\":\"") != 0) return -1;
                if (harness_json_escape_append(buf, cap, &used, tc->name) != 0) return -1;
                if (harness_json_append_raw(buf, cap, &used, "\",\"arguments\":\"") != 0)
                    return -1;
                if (harness_json_escape_append(buf, cap, &used, tc->arguments) != 0) return -1;
                if (harness_json_append_raw(buf, cap, &used, "\"}") != 0) return -1;
            }
            if (harness_json_append_raw(buf, cap, &used, "]") != 0) return -1;
        }
        if (harness_json_append_raw(buf, cap, &used, "}") != 0) return -1;
    }
    if (harness_json_append_raw(buf, cap, &used, "]}") != 0) return -1;
    if (used >= cap) return -1;
    buf[used] = '\0';
    if (out_len) *out_len = used;
    return 0;
}

/* Minimal import: only flat string content messages (not full tool_calls rehydration). */
int harness_history_import_json(harness_ctx_t* ctx, const char* json, size_t len) {
    const char* p;
    const char* end;
    if (!ctx || !json) return -1;
    if (ctx->session_retired) return -1;
    (void)len;
    /* Clear existing messages */
    memset(ctx->messages, 0, ctx->message_cap * sizeof(*ctx->messages));
    ctx->message_count = 0;

    p = json;
    end = json + (len ? len : strlen(json));
    while (p < end) {
        const char* peer_k = strstr(p, "\"peer_id\"");
        const char* role_k;
        const char* content_k;
        char peer[HARNESS_PEER_ID_CAP];
        char role[32];
        char content[HARNESS_MSG_CONTENT_MAX];
        harness_message_role_t r;
        bool secret = false;

        peer[0] = role[0] = content[0] = '\0';
        if (!peer_k || peer_k >= end) break;
        /* extract peer */
        {
            const char* q = strchr(peer_k, ':');
            if (!q) break;
            q = strchr(q, '"');
            if (!q) break;
            q++;
            {
                const char* e = strchr(q, '"');
                size_t n;
                if (!e) break;
                n = (size_t)(e - q);
                if (n >= sizeof(peer)) n = sizeof(peer) - 1;
                memcpy(peer, q, n);
                peer[n] = '\0';
                p = e + 1;
            }
        }
        role_k = strstr(p, "\"role\"");
        if (!role_k) break;
        {
            const char* q = strchr(role_k, ':');
            if (!q) break;
            q = strchr(q, '"');
            if (!q) break;
            q++;
            {
                const char* e = strchr(q, '"');
                size_t n;
                if (!e) break;
                n = (size_t)(e - q);
                if (n >= sizeof(role)) n = sizeof(role) - 1;
                memcpy(role, q, n);
                role[n] = '\0';
                p = e + 1;
            }
        }
        content_k = strstr(p, "\"content\"");
        if (!content_k) break;
        {
            const char* q = strchr(content_k, ':');
            if (!q) break;
            q++;
            while (*q && isspace((unsigned char)*q)) q++;
            if (*q == '"') {
                const char* e;
                size_t n;
                q++;
                e = q;
                while (*e && *e != '"') {
                    if (*e == '\\' && e[1]) e += 2;
                    else e++;
                }
                if (*e != '"') break;
                n = (size_t)(e - q);
                if (n >= sizeof(content)) n = sizeof(content) - 1;
                /* naive copy without unescape for dialectic tests */
                memcpy(content, q, n);
                content[n] = '\0';
                p = e + 1;
            } else {
                /* skip array/object content for this simple importer */
                p = content_k + 9;
                continue;
            }
        }
        if (strstr(p, "\"is_secret\":true") == p || strstr(p, "\"is_secret\": true") != NULL) {
            /* look nearby only */
            const char* s = strstr(p, "is_secret");
            if (s && s < p + 80 && strstr(s, "true")) secret = true;
        }
        if (harness_msg_role_from_name(role, &r) != 0) r = HARNESS_MSG_USER;
        if (harness_message_append(ctx, peer, r, content, secret) != 0) return -1;
    }
    harness_emit(ctx, HARNESS_EVENT_HISTORY_READY, NULL, NULL, 0, ctx->message_count);
    return 0;
}

int harness_response_stream_begin(harness_ctx_t* ctx) {
    if (!ctx) return -1;
    ctx->stream_active = true;
    ctx->stream_len = 0;
    if (!ctx->stream_buf) {
        ctx->stream_cap = HARNESS_STREAM_DEFAULT_CAP;
        ctx->stream_buf = (uint8_t*)malloc(ctx->stream_cap);
        if (!ctx->stream_buf) {
            ctx->stream_active = false;
            return -1;
        }
    }
    return 0;
}

int harness_response_stream_feed(harness_ctx_t* ctx, const uint8_t* data, size_t len) {
    if (!ctx || !ctx->stream_active || !data) return -1;
    if (ctx->stream_len + len > ctx->stream_cap) {
        size_t ncap = ctx->stream_cap * 2;
        uint8_t* n;
        while (ncap < ctx->stream_len + len) ncap *= 2;
        n = (uint8_t*)realloc(ctx->stream_buf, ncap);
        if (!n) return -1;
        ctx->stream_buf = n;
        ctx->stream_cap = ncap;
    }
    memcpy(ctx->stream_buf + ctx->stream_len, data, len);
    ctx->stream_len += len;
    return 0;
}

int harness_response_stream_finish(harness_ctx_t* ctx) {
    int rc;
    if (!ctx || !ctx->stream_active) return -1;
    ctx->stream_active = false;
    rc = harness_response_parse(ctx, ctx->stream_buf, ctx->stream_len);
    harness_emit(ctx, HARNESS_EVENT_STREAM_FINISHED, NULL, NULL, rc, ctx->stream_len);
    return rc;
}

size_t harness_log_count(const harness_ctx_t* ctx) {
    return ctx ? ctx->log_count : 0;
}

int harness_log_get(const harness_ctx_t* ctx,
                    size_t index,
                    harness_interaction_record_t* out) {
    size_t start;
    if (!ctx || !out || index >= ctx->log_count) return -1;
    if (ctx->log_count < HARNESS_LOG_SLOTS)
        start = 0;
    else
        start = ctx->log_head; /* oldest when full */
    {
        size_t idx = (start + index) % HARNESS_LOG_SLOTS;
        *out = ctx->log_ring[idx];
    }
    return 0;
}

int harness_log_export_json(const harness_ctx_t* ctx,
                            char* buf,
                            size_t cap,
                            size_t* out_len) {
    size_t used = 0;
    size_t i;
    if (!ctx || !buf || cap == 0) return -1;
    if (harness_json_append_raw(buf, cap, &used, "{\"interactions\":[") != 0) return -1;
    for (i = 0; i < ctx->log_count; i++) {
        harness_interaction_record_t rec;
        char num[64];
        if (harness_log_get(ctx, i, &rec) != 0) return -1;
        if (i > 0 && harness_json_append_raw(buf, cap, &used, ",") != 0) return -1;
        if (harness_json_append_raw(buf, cap, &used, "{\"seq\":") != 0) return -1;
        snprintf(num, sizeof(num), "%llu", (unsigned long long)rec.seq);
        if (harness_json_append_raw(buf, cap, &used, num) != 0) return -1;
        if (harness_json_append_raw(buf, cap, &used, ",\"model\":\"") != 0) return -1;
        if (harness_json_escape_append(buf, cap, &used, rec.model) != 0) return -1;
        if (harness_json_append_raw(buf, cap, &used, "\",\"session_id\":\"") != 0) return -1;
        if (harness_json_escape_append(buf, cap, &used, rec.session_id) != 0) return -1;
        if (harness_json_append_raw(buf, cap, &used, "\",\"acting_peer_id\":\"") != 0)
            return -1;
        if (harness_json_escape_append(buf, cap, &used, rec.acting_peer_id) != 0) return -1;
        snprintf(num, sizeof(num),
                 "\",\"prompt_tokens\":%u,\"completion_tokens\":%u,\"total_tokens\":%u}",
                 (unsigned)rec.prompt_tokens, (unsigned)rec.completion_tokens,
                 (unsigned)rec.total_tokens);
        if (harness_json_append_raw(buf, cap, &used, num) != 0) return -1;
    }
    if (harness_json_append_raw(buf, cap, &used, "]}") != 0) return -1;
    if (used >= cap) return -1;
    buf[used] = '\0';
    if (out_len) *out_len = used;
    return 0;
}

/* Called from harness_log_interaction path via weak-style public API in harness.c —
 * we provide a helper used by pique_integration. */
void harness_log_record_push(harness_ctx_t* ctx, const char* model) {
    harness_interaction_record_t* rec;
    if (!ctx) return;
    rec = &ctx->log_ring[ctx->log_head];
    memset(rec, 0, sizeof(*rec));
    harness_copy_id(rec->model, sizeof(rec->model), model ? model : "unknown");
    harness_copy_id(rec->session_id, sizeof(rec->session_id), ctx->session_id);
    harness_copy_id(rec->acting_peer_id, sizeof(rec->acting_peer_id), ctx->acting_peer_id);
    rec->prompt_tokens = ctx->last_usage.prompt_tokens;
    rec->completion_tokens = ctx->last_usage.completion_tokens;
    rec->total_tokens = ctx->last_usage.total_tokens;
    rec->seq = ctx->interactions_logged;
    ctx->log_head = (ctx->log_head + 1) % HARNESS_LOG_SLOTS;
    if (ctx->log_count < HARNESS_LOG_SLOTS) ctx->log_count++;
}
