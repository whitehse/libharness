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

/* ---- PG log redaction + pique feed + vector SQL + compress_select ---- */

int harness_redact_text_for_log(const harness_ctx_t* ctx, char* text, size_t cap) {
    size_t i;
    int replaced = 0;
    if (!ctx || !text || cap == 0) return -1;
    for (i = 0; i < ctx->message_count; i++) {
        const harness_message_slot_t* m = &ctx->messages[i];
        char* hit;
        char ref[64];
        size_t secret_len;
        size_t ref_len;
        size_t tail_len;
        if (!m->in_use || !m->is_secret || m->content[0] == '\0') continue;
        secret_len = strlen(m->content);
        if (secret_len == 0) continue;
        snprintf(ref, sizeof(ref), "[secret_ref:%u]", (unsigned)m->secret_ref_id);
        ref_len = strlen(ref);
        while ((hit = strstr(text, m->content)) != NULL) {
            size_t prefix = (size_t)(hit - text);
            tail_len = strlen(hit + secret_len);
            if (prefix + ref_len + tail_len + 1 > cap) return -1;
            memmove(hit + ref_len, hit + secret_len, tail_len + 1);
            memcpy(hit, ref, ref_len);
            replaced++;
        }
    }
    return replaced;
}

int harness_history_compress_select(harness_ctx_t* ctx,
                                    const uint8_t* keep_mask,
                                    size_t mask_len) {
    size_t i;
    size_t w = 0;
    size_t dropped = 0;
    if (!ctx || !keep_mask || mask_len != ctx->message_count) return -1;
    for (i = 0; i < ctx->message_count; i++) {
        if (keep_mask[i]) {
            if (w != i)
                ctx->messages[w] = ctx->messages[i];
            w++;
        } else {
            dropped++;
        }
    }
    for (i = w; i < ctx->message_count; i++)
        memset(&ctx->messages[i], 0, sizeof(ctx->messages[i]));
    ctx->message_count = w;
    harness_emit_ex(ctx, HARNESS_EVENT_HISTORY_COMPRESSED, NULL, NULL,
                    (int)dropped, ctx->message_count, "vector_select");
    harness_emit_ex(ctx, HARNESS_EVENT_VECTOR_CLASSIFIED, NULL, NULL,
                    (int)dropped, mask_len, "compress_select");
    return 0;
}

int harness_history_keep_mask_from_scores(const float* scores,
                                          size_t score_len,
                                          size_t keep_count,
                                          uint8_t* keep_mask,
                                          size_t mask_len) {
    size_t i;
    size_t kept = 0;
    if (!scores || !keep_mask || score_len == 0 || mask_len != score_len)
        return -1;
    memset(keep_mask, 0, mask_len);
    if (keep_count == 0) keep_count = 1;
    if (keep_count > score_len) keep_count = score_len;

    /* O(n * keep) selection: repeatedly pick highest remaining score,
     * breaking ties toward the higher (more recent) index. */
    while (kept < keep_count) {
        size_t best = (size_t)-1;
        for (i = 0; i < score_len; i++) {
            if (keep_mask[i]) continue;
            if (best == (size_t)-1 ||
                scores[i] > scores[best] ||
                (scores[i] == scores[best] && i > best))
                best = i;
        }
        if (best == (size_t)-1) break;
        keep_mask[best] = 1;
        kept++;
    }
    return 0;
}

int harness_history_compress_by_scores(harness_ctx_t* ctx,
                                       const float* scores,
                                       size_t score_len,
                                       size_t keep_count) {
    uint8_t* mask;
    int rc;
    if (!ctx || !scores || score_len != ctx->message_count) return -1;
    mask = (uint8_t*)malloc(score_len ? score_len : 1);
    if (!mask) return -1;
    if (harness_history_keep_mask_from_scores(scores, score_len, keep_count,
                                              mask, score_len) != 0) {
        free(mask);
        return -1;
    }
    rc = harness_history_compress_select(ctx, mask, score_len);
    free(mask);
    if (rc == 0)
        harness_emit_ex(ctx, HARNESS_EVENT_VECTOR_CLASSIFIED, NULL, NULL,
                        (int)keep_count, score_len, "compress_by_scores");
    return rc;
}

int harness_pique_build_embedding_insert(const harness_ctx_t* ctx,
                                         const char* collection,
                                         const char* text,
                                         const char* embedding_sql_literal,
                                         char* buf,
                                         size_t cap,
                                         size_t* out_len) {
    size_t used = 0;
    if (!ctx || !collection || !text || !embedding_sql_literal || !buf || cap == 0)
        return -1;
    if (harness_json_append_raw(buf, cap, &used,
            "INSERT INTO harness_embeddings "
            "(session_id, workspace_id, collection, text, embedding) VALUES ('") != 0)
        return -1;
    if (sql_escape_append(buf, cap, &used, ctx->session_id) != 0) return -1;
    if (harness_json_append_raw(buf, cap, &used, "','") != 0) return -1;
    if (sql_escape_append(buf, cap, &used, ctx->workspace_id) != 0) return -1;
    if (harness_json_append_raw(buf, cap, &used, "','") != 0) return -1;
    if (sql_escape_append(buf, cap, &used, collection) != 0) return -1;
    if (harness_json_append_raw(buf, cap, &used, "','") != 0) return -1;
    if (sql_escape_append(buf, cap, &used, text) != 0) return -1;
    if (harness_json_append_raw(buf, cap, &used, "',") != 0) return -1;
    /* embedding_sql_literal is trusted SQL fragment from caller, not user text */
    if (harness_json_append_raw(buf, cap, &used, embedding_sql_literal) != 0) return -1;
    if (harness_json_append_raw(buf, cap, &used, ");") != 0) return -1;
    if (used >= cap) return -1;
    buf[used] = '\0';
    if (out_len) *out_len = used;
    return 0;
}

int harness_pique_build_similarity_search(const harness_ctx_t* ctx,
                                          const char* collection,
                                          const char* embedding_sql_literal,
                                          size_t limit,
                                          char* buf,
                                          size_t cap,
                                          size_t* out_len) {
    size_t used = 0;
    char lim[32];
    if (!ctx || !collection || !embedding_sql_literal || !buf || cap == 0) return -1;
    if (limit == 0) limit = 8;
    if (harness_json_append_raw(buf, cap, &used,
            "SELECT id, text, 1 - (embedding <=> ") != 0)
        return -1;
    if (harness_json_append_raw(buf, cap, &used, embedding_sql_literal) != 0) return -1;
    if (harness_json_append_raw(buf, cap, &used,
            ") AS score FROM harness_embeddings WHERE collection='") != 0)
        return -1;
    if (sql_escape_append(buf, cap, &used, collection) != 0) return -1;
    if (ctx->session_id[0]) {
        if (harness_json_append_raw(buf, cap, &used, "' AND session_id='") != 0)
            return -1;
        if (sql_escape_append(buf, cap, &used, ctx->session_id) != 0) return -1;
    }
    if (harness_json_append_raw(buf, cap, &used, "' ORDER BY embedding <=> ") != 0)
        return -1;
    if (harness_json_append_raw(buf, cap, &used, embedding_sql_literal) != 0) return -1;
    snprintf(lim, sizeof(lim), " LIMIT %zu;", limit);
    if (harness_json_append_raw(buf, cap, &used, lim) != 0) return -1;
    if (used >= cap) return -1;
    buf[used] = '\0';
    if (out_len) *out_len = used;
    return 0;
}

int harness_pique_feed_sql(harness_ctx_t* ctx, const char* sql, size_t len) {
    if (!ctx || !sql) return -1;
    if (len == 0) len = strlen(sql);
    if (harness_set_output(ctx, sql, len) != 0) return -1;
    ctx->state = HARNESS_STATE_LOGGING;
    harness_emit_ex(ctx, HARNESS_EVENT_PIQUE_SQL_READY, NULL, NULL, 0, len, "sql");
    return 0;
}

int harness_pique_feed_log(harness_ctx_t* ctx,
                           const char* model,
                           const char* prompt,
                           const char* response) {
    char* sql = NULL;
    size_t cap = 8192;
    size_t n = 0;
    char* pbuf = NULL;
    char* rbuf = NULL;
    size_t plen;
    size_t rlen;
    int rc = -1;

    if (!ctx || !model) return -1;

    plen = prompt ? strlen(prompt) : 0;
    rlen = response ? strlen(response) : 0;
    pbuf = (char*)malloc(plen + 1 + 256);
    rbuf = (char*)malloc(rlen + 1 + 256);
    sql = (char*)malloc(cap);
    if (!pbuf || !rbuf || !sql) goto done;

    if (prompt) memcpy(pbuf, prompt, plen + 1);
    else pbuf[0] = '\0';
    if (response) memcpy(rbuf, response, rlen + 1);
    else rbuf[0] = '\0';

    if (ctx->config.redact_secrets_in_log) {
        if (harness_redact_text_for_log(ctx, pbuf, plen + 1 + 256) < 0) goto done;
        if (harness_redact_text_for_log(ctx, rbuf, rlen + 1 + 256) < 0) goto done;
    }

    if (harness_pique_build_log_insert(ctx, model, pbuf, rbuf, sql, cap, &n) != 0)
        goto done;
    if (harness_pique_feed_sql(ctx, sql, n) != 0) goto done;
    (void)harness_log_interaction(ctx, model, prompt, response);
    harness_emit_ex(ctx, HARNESS_EVENT_PIQUE_FEED_STAGED, ctx->acting_peer_id, NULL,
                    0, n, "log_insert");
    rc = 0;
done:
    free(pbuf);
    free(rbuf);
    free(sql);
    return rc;
}

int harness_pique_feed_session(harness_ctx_t* ctx) {
    char sql[2048];
    size_t n = 0;
    if (!ctx) return -1;
    if (harness_pique_build_session_upsert(ctx, sql, sizeof(sql), &n) != 0) return -1;
    if (harness_pique_feed_sql(ctx, sql, n) != 0) return -1;
    harness_emit_ex(ctx, HARNESS_EVENT_PIQUE_FEED_STAGED, NULL, NULL, 0, n,
                    "session_upsert");
    return 0;
}

int harness_pique_feed_embedding(harness_ctx_t* ctx,
                                 const char* collection,
                                 const char* text,
                                 const char* embedding_sql_literal) {
    char* sql = NULL;
    size_t cap = 4096;
    size_t n = 0;
    int rc = -1;
    if (!ctx || !collection || !text || !embedding_sql_literal) return -1;
    sql = (char*)malloc(cap);
    if (!sql) return -1;
    if (harness_pique_build_embedding_insert(ctx, collection, text,
            embedding_sql_literal, sql, cap, &n) != 0)
        goto done;
    if (harness_pique_feed_sql(ctx, sql, n) != 0) goto done;
    harness_emit_ex(ctx, HARNESS_EVENT_PIQUE_FEED_STAGED, collection, NULL, 0, n,
                    "embedding_insert");
    harness_emit_ex(ctx, HARNESS_EVENT_VECTOR_CLASSIFIED, collection, NULL, 0, 0,
                    "embedding_insert");
    rc = 0;
done:
    free(sql);
    return rc;
}

int harness_pique_feed_similarity(harness_ctx_t* ctx,
                                  const char* collection,
                                  const char* embedding_sql_literal,
                                  size_t limit) {
    char* sql = NULL;
    size_t cap = 4096;
    size_t n = 0;
    int rc = -1;
    if (!ctx || !collection || !embedding_sql_literal) return -1;
    sql = (char*)malloc(cap);
    if (!sql) return -1;
    if (harness_pique_build_similarity_search(ctx, collection, embedding_sql_literal,
            limit, sql, cap, &n) != 0)
        goto done;
    if (harness_pique_feed_sql(ctx, sql, n) != 0) goto done;
    harness_emit_ex(ctx, HARNESS_EVENT_PIQUE_FEED_STAGED, collection, NULL, 0, n,
                    "similarity_search");
    rc = 0;
done:
    free(sql);
    return rc;
}

/* Parse one numeric score prefix (possibly float or fixed-point int). */
static int parse_score_milli(const char* s, int* out_milli) {
    double v = 0.0;
    char* end = NULL;
    if (!s || !out_milli) return -1;
    while (*s == ' ' || *s == '\t') s++;
    v = strtod(s, &end);
    if (end == s) return -1;
    if (v < -1e6) v = -1e6;
    if (v > 1e6) v = 1e6;
    *out_milli = (int)(v * 1000.0 + (v >= 0 ? 0.5 : -0.5));
    return 0;
}

int harness_pique_parse_similarity_tsv(harness_ctx_t* ctx,
                                       const char* data,
                                       size_t len) {
    size_t i = 0;
    size_t rows = 0;
    if (!ctx || !data) return -1;
    if (len == 0) len = strlen(data);
    ctx->state = HARNESS_STATE_VECTOR_OP;

    while (i < len) {
        size_t line_start = i;
        size_t line_end;
        char line[HARNESS_EVENT_DETAIL_CAP + 256];
        size_t copy_n;
        char* p;
        char* tab;
        char* pipe;
        int score_milli = 0;
        const char* text_part;
        int used_pipe = 0;

        while (i < len && data[i] != '\n' && data[i] != '\r') i++;
        line_end = i;
        while (i < len && (data[i] == '\n' || data[i] == '\r')) i++;
        if (line_end == line_start) continue;

        copy_n = line_end - line_start;
        if (copy_n >= sizeof(line)) copy_n = sizeof(line) - 1;
        memcpy(line, data + line_start, copy_n);
        line[copy_n] = '\0';

        p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#') continue;

        /* Prefer tab form score\ttext; else pipe form score|id|text */
        tab = strchr(p, '\t');
        pipe = strchr(p, '|');
        if (tab && (!pipe || tab < pipe)) {
            *tab = '\0';
            text_part = tab + 1;
        } else if (pipe) {
            *pipe = '\0';
            text_part = pipe + 1;
            used_pipe = 1;
            /* optional id field: score|id|text → use field after second | */
            {
                char* pipe2 = strchr(text_part, '|');
                if (pipe2) text_part = pipe2 + 1;
            }
            (void)used_pipe;
        } else {
            continue;
        }

        if (parse_score_milli(p, &score_milli) != 0) continue;
        while (*text_part == ' ' || *text_part == '\t') text_part++;
        if (*text_part == '\0') text_part = "(empty)";

        harness_emit_ex(ctx, HARNESS_EVENT_VECTOR_HIT, ctx->session_id, NULL,
                        score_milli, rows, text_part);
        rows++;
    }

    harness_emit_ex(ctx, HARNESS_EVENT_VECTOR_CLASSIFIED, NULL, NULL,
                    (int)rows, rows, "similarity_tsv");
    return (int)rows;
}

/* Alias for app pipelines that unpack pqwire DATA_ROW fields to TSV/pipe text. */
int harness_pique_parse_data_rows(harness_ctx_t* ctx,
                                  const char* data,
                                  size_t len) {
    return harness_pique_parse_similarity_tsv(ctx, data, len);
}
