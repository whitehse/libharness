/* Offline dialectic for live embedding scoring path (TODO 2.3).
 * Real float vectors → SQL literal → feed → mock TSV scores → compress.
 * No remote PG; caller-owned wire remains outside core.
 */
#include "harness.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    harness_config_t cfg;
    harness_ctx_t* ctx;
    float dims[3] = {0.1f, -0.25f, 0.5f};
    char lit[128];
    size_t lit_len = 0;
    char sql[2048];
    size_t n = 0;
    uint8_t out[4096];
    size_t out_len = 0;
    float scores[8];
    size_t score_count = 0;
    char peer[HARNESS_PEER_ID_CAP];
    char content[256];
    harness_message_role_t role = HARNESS_MSG_SYSTEM;
    bool secret = true;
    size_t i;

    printf("vector scoring dialectic...\n");
    harness_config_init_defaults(&cfg);
    cfg.workspace_id = "ws_vec";
    cfg.session_id = "sess_vec";
    cfg.acting_peer_id = "agent_a";
    ctx = harness_create_with_config(HARNESS_ROLE_MAIN, &cfg);
    assert(ctx);
    assert(strcmp(harness_version(), "0.9.0-todo-impl") == 0 ||
           strstr(harness_version(), "0.9") != NULL);

    assert(harness_participant_add(ctx, "human_a", HARNESS_PARTICIPANT_HUMAN, false) == 0);
    assert(harness_participant_add(ctx, "agent_a", HARNESS_PARTICIPANT_AGENT, true) == 0);

    assert(harness_message_append(ctx, "human_a", HARNESS_MSG_USER, "noise-low", false) == 0);
    assert(harness_message_append(ctx, "human_a", HARNESS_MSG_USER, "mid-signal", false) == 0);
    assert(harness_message_append(ctx, "human_a", HARNESS_MSG_USER, "best-hit", false) == 0);
    assert(harness_message_append(ctx, "agent_a", HARNESS_MSG_ASSISTANT, "ack", false) == 0);
    assert(harness_message_count(ctx) == 4);

    /* message_get for embedding pipeline */
    assert(harness_message_get(ctx, 2, peer, sizeof(peer), &role, content, sizeof(content),
                               &secret) == 0);
    assert(strcmp(peer, "human_a") == 0);
    assert(role == HARNESS_MSG_USER);
    assert(strcmp(content, "best-hit") == 0);
    assert(secret == false);
    assert(harness_message_get(ctx, 99, NULL, 0, NULL, NULL, 0, NULL) == -1);
    printf("  message_get: OK\n");

    /* Real floats → pgvector SQL literal */
    assert(harness_pique_format_vector_literal(dims, 3, lit, sizeof(lit), &lit_len) == 0);
    assert(strstr(lit, "'[") != NULL);
    assert(strstr(lit, "]'::vector") != NULL);
    assert(strstr(lit, "0.1") != NULL);
    assert(strstr(lit, "-0.25") != NULL);
    assert(harness_pique_format_vector_literal(NULL, 3, lit, sizeof(lit), &lit_len) == -1);
    assert(harness_pique_format_vector_literal(dims, 0, lit, sizeof(lit), &lit_len) == -1);
    printf("  format_vector_literal: OK (%s)\n", lit);

    /* Feed embedding + similarity with real literal (no network) */
    assert(harness_pique_feed_embedding(ctx, "history", content, lit) == 0);
    assert(harness_get_output(ctx, out, sizeof(out) - 1, &out_len) == 0);
    out[out_len] = '\0';
    assert(strstr((char*)out, "harness_embeddings") != NULL);
    assert(strstr((char*)out, lit) != NULL || strstr((char*)out, "0.1") != NULL);

    assert(harness_pique_build_similarity_search(ctx, "history", lit, 4, sql, sizeof(sql), &n) == 0);
    assert(strstr(sql, "ORDER BY") != NULL);
    assert(strstr(sql, "]'::vector") != NULL);
    assert(harness_pique_feed_similarity(ctx, "history", lit, 4) == 0);
    printf("  feed with real vector literal: OK\n");

    /* Mock PG result → scores[] without events */
    {
        const char* tsv =
            "0.12\tnoise-low\n"
            "0.55\tmid-signal\n"
            "0.97\tbest-hit\n"
            "0.40|7|ack\n"
            "# skip\n";
        assert(harness_pique_parse_similarity_scores(tsv, 0, scores, 8, &score_count) == 0);
        assert(score_count == 4);
        assert(scores[0] > 0.11f && scores[0] < 0.13f);
        assert(scores[2] > 0.96f && scores[2] < 0.98f);
        assert(scores[3] > 0.39f && scores[3] < 0.41f);
    }
    printf("  parse_similarity_scores: OK (n=%zu)\n", score_count);

    /* Align one score per message (mock caller mapping after live query) */
    assert(score_count == harness_message_count(ctx));
    assert(harness_history_compress_by_scores(ctx, scores, score_count, 2) == 0);
    assert(harness_message_count(ctx) == 2);

    /* Kept highest: best-hit (0.97) and mid-signal (0.55) or ack (0.40)? top-2 = 0.97,0.55 */
    {
        int saw_best = 0;
        int saw_mid = 0;
        for (i = 0; i < harness_message_count(ctx); i++) {
            assert(harness_message_get(ctx, i, NULL, 0, NULL, content, sizeof(content), NULL) == 0);
            if (strcmp(content, "best-hit") == 0) saw_best = 1;
            if (strcmp(content, "mid-signal") == 0) saw_mid = 1;
        }
        assert(saw_best && saw_mid);
    }
    printf("  compress_by_scores after live-shaped scores: OK\n");

    /* Cap truncate path */
    {
        const char* two = "1.0\ta\n2.0\tb\n3.0\tc\n";
        size_t c = 0;
        assert(harness_pique_parse_similarity_scores(two, 0, scores, 2, &c) == 0);
        assert(c == 2);
    }

    harness_destroy(ctx);
    printf("vector scoring dialectic PASSED\n");
    return 0;
}
