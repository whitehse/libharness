/* History export/import, mute, multipart, stream, log ring tests */
#include "harness.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    harness_config_t cfg;
    harness_ctx_t* ctx;
    harness_context_params_t params;
    harness_content_part_t parts[2];
    harness_interaction_record_t rec;
    uint8_t caller_out[4096];
    char hist[8192];
    char logj[4096];
    size_t n = 0;
    uint8_t out[8192];
    size_t out_len = 0;
    const char* chunk1 = "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"";
    const char* chunk2 = "Hello stream\"}}],\"usage\":{\"prompt_tokens\":1,\"completion_tokens\":2,\"total_tokens\":3}}";

    printf("history/stream/mute test...\n");
    harness_config_init_defaults(&cfg);
    cfg.caller_output_buf = caller_out;
    cfg.caller_output_cap = sizeof(caller_out);
    cfg.session_id = "s1";
    cfg.acting_peer_id = "agent_a";

    ctx = harness_create_with_config(HARNESS_ROLE_MAIN, &cfg);
    assert(ctx);
    assert(harness_participant_add(ctx, "human_a", HARNESS_PARTICIPANT_HUMAN, true) == 0);
    assert(harness_participant_add(ctx, "noise_bot", HARNESS_PARTICIPANT_AGENT, false) == 0);
    assert(harness_participant_set_muted(ctx, "noise_bot", true) == 0);
    assert(harness_participant_is_muted(ctx, "noise_bot"));

    assert(harness_message_append(ctx, "human_a", HARNESS_MSG_USER, "hi", false) == 0);
    assert(harness_message_append(ctx, "noise_bot", HARNESS_MSG_USER, "muted spam", false) == 0);

    memset(parts, 0, sizeof(parts));
    parts[0].type = "text";
    parts[0].text = "see image";
    parts[1].type = "image_url";
    parts[1].image_url = "https://example.com/x.png";
    assert(harness_message_append_parts(ctx, "human_a", HARNESS_MSG_USER, parts, 2, false) == 0);

    memset(&params, 0, sizeof(params));
    params.model = "gpt-4o";
    params.temperature = 0;
    params.include_tools = false;
    params.identity_prefix = true;
    params.redact_secrets = true;
    assert(harness_set_acting_peer(ctx, "agent_a") == 0);
    assert(harness_context_build(ctx, &params) == 0);
    assert(harness_get_output(ctx, out, sizeof(out) - 1, &out_len) == 0);
    out[out_len] = '\0';
    assert(strstr((char*)out, "muted spam") == NULL);
    assert(strstr((char*)out, "image_url") != NULL);
    assert(strstr((char*)out, "https://example.com/x.png") != NULL);
    /* caller buffer used */
    assert(memcmp(caller_out, out, out_len) == 0);
    printf("  mute + multipart + caller_output: OK\n");

    assert(harness_history_export_json(ctx, hist, sizeof(hist), &n) == 0);
    assert(strstr(hist, "human_a") != NULL);
    {
        harness_ctx_t* ctx2 = harness_create(HARNESS_ROLE_MAIN);
        assert(ctx2);
        assert(harness_history_import_json(ctx2, hist, n) == 0);
        assert(harness_message_count(ctx2) >= 1);
        harness_destroy(ctx2);
    }
    printf("  history export/import: OK\n");

    assert(harness_response_stream_begin(ctx) == 0);
    assert(harness_response_stream_feed(ctx, (const uint8_t*)chunk1, strlen(chunk1)) == 0);
    assert(harness_response_stream_feed(ctx, (const uint8_t*)chunk2, strlen(chunk2)) == 0);
    assert(harness_response_stream_finish(ctx) == 0);
    assert(harness_response_status(ctx) == HARNESS_RESPONSE_COMPLETED);
    assert(strstr(harness_response_assistant_text(ctx), "Hello stream") != NULL);
    printf("  stream assembly: OK\n");

    assert(harness_log_interaction(ctx, "gpt-4o", "p", "r") == 0);
    assert(harness_log_count(ctx) == 1);
    assert(harness_log_get(ctx, 0, &rec) == 0);
    assert(strcmp(rec.model, "gpt-4o") == 0);
    assert(harness_log_export_json(ctx, logj, sizeof(logj), &n) == 0);
    assert(strstr(logj, "interactions") != NULL);
    printf("  interaction log ring: OK\n");

    {
        char meta[128];
        size_t mn = 0;
        assert(harness_honcho_metadata_chat(meta, sizeof(meta), &mn) == 0);
        assert(strstr(meta, "chat_message") != NULL);
        assert(harness_honcho_metadata_agent(meta, sizeof(meta), &mn, "gpt-4o") == 0);
        assert(strstr(meta, "agent_response") != NULL);
        assert(harness_honcho_should_mirror(ctx, "hello team") == true);
        assert(harness_honcho_should_mirror(ctx, "{\"tool_calls\":[]}") == false);
        assert(harness_honcho_parse_response(ctx,
            (const uint8_t*)"{\"messages\":[{\"peer_id\":\"human_a\",\"content\":\"yo\"}]}",
            56) == 0);
    }
    printf("  honcho helpers: OK\n");

    harness_destroy(ctx);
    printf("history/stream/mute test PASSED\n");
    return 0;
}
