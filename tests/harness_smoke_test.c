#include "harness.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static void drain_events(harness_ctx_t* ctx) {
    harness_event_t ev;
    while (harness_next_event(ctx, &ev) == 0 && ev.type != HARNESS_EVENT_NONE) {
        /* drain */
    }
}

int main(void) {
    harness_config_t cfg;
    harness_ctx_t* ctx;
    harness_context_params_t params;
    harness_event_t ev;
    uint8_t out[8192];
    size_t out_len = 0;
    int rc;
    const char* resp_completed =
        "{\"id\":\"resp_1\",\"status\":\"completed\",\"output\":[{\"type\":\"message\","
        "\"role\":\"assistant\",\"content\":[{\"type\":\"text\",\"text\":\"ok\"}]}]}";
    const char* resp_tools =
        "{\"status\":\"requires_action\",\"output\":[{\"type\":\"function_call\","
        "\"call_id\":\"call_abc\",\"name\":\"get_stock_price\"}]}";
    char prefix[64];

    printf("libharness ADR 002 smoke test starting...\n");

    harness_config_init_defaults(&cfg);
    cfg.workspace_id = "ws_demo";
    cfg.session_id = "sess_deploy";
    cfg.acting_peer_id = "agent_devbot";

    ctx = harness_create_with_config(HARNESS_ROLE_MAIN, &cfg);
    assert(ctx != NULL);
    printf("  create_with_config: OK\n");

    assert(harness_version() != NULL);
    printf("  version: %s\n", harness_version());

    rc = harness_session_set(ctx, "ws_demo", "sess_deploy");
    assert(rc == 0);

    rc = harness_participant_add(ctx, "human_alice", HARNESS_PARTICIPANT_HUMAN, true);
    assert(rc == 0);
    rc = harness_participant_add(ctx, "human_bob", HARNESS_PARTICIPANT_HUMAN, false);
    assert(rc == 0);
    rc = harness_participant_add(ctx, "agent_devbot", HARNESS_PARTICIPANT_AGENT, true);
    assert(rc == 0);
    assert(harness_participant_count(ctx) == 3);
    printf("  participants: OK (%zu)\n", harness_participant_count(ctx));

    rc = harness_soul_set(ctx, "You are DevBot, careful about production.");
    assert(rc == 0);

    rc = harness_message_append(ctx, "human_alice", HARNESS_MSG_USER,
                                "Should we deploy the migration tonight?", false);
    assert(rc == 0);
    rc = harness_message_append(ctx, "human_bob", HARNESS_MSG_USER,
                                "db_password=s3cret", true);
    assert(rc == 0);
    assert(harness_message_count(ctx) == 2);
    assert(harness_message_secret_ref(ctx, 1) != 0);

    rc = harness_format_identity_prefix("human_alice", prefix, sizeof(prefix));
    assert(rc > 0);
    assert(strcmp(prefix, "[human_alice]") == 0);
    printf("  identity_prefix: %s\n", prefix);

    rc = harness_tool_register_json(ctx,
        "{\"type\":\"function\",\"function\":{\"name\":\"get_stock_price\","
        "\"description\":\"price\",\"parameters\":{\"type\":\"object\",\"properties\":{}}}}");
    assert(rc == 0);
    assert(harness_tool_count(ctx) == 1);

    memset(&params, 0, sizeof(params));
    params.model = "gpt-4o";
    params.temperature = 0.2;
    params.include_tools = true;
    params.identity_prefix = true;
    params.redact_secrets = true;

    /* Acting as non-privileged bob → secrets redacted */
    assert(harness_set_acting_peer(ctx, "human_bob") == 0);
    rc = harness_context_build(ctx, &params);
    assert(rc == 0);
    assert(harness_get_output(ctx, out, sizeof(out), &out_len) == 0);
    assert(out_len > 0);
    out[out_len < sizeof(out) ? out_len : sizeof(out) - 1] = '\0';
    assert(strstr((char*)out, "[human_alice]") != NULL);
    assert(strstr((char*)out, "secret:ref_") != NULL);
    assert(strstr((char*)out, "s3cret") == NULL);
    assert(strstr((char*)out, "get_stock_price") != NULL);
    assert(strstr((char*)out, "\"model\":\"gpt-4o\"") != NULL);
    printf("  context_build (redacted): OK (%zu bytes)\n", out_len);

    drain_events(ctx);

    /* Privileged agent sees secret value */
    assert(harness_set_acting_peer(ctx, "agent_devbot") == 0);
    rc = harness_context_build(ctx, &params);
    assert(rc == 0);
    assert(harness_get_output(ctx, out, sizeof(out), &out_len) == 0);
    out[out_len < sizeof(out) ? out_len : sizeof(out) - 1] = '\0';
    assert(strstr((char*)out, "s3cret") != NULL);
    printf("  context_build (privileged): OK\n");

    rc = harness_response_parse(ctx, (const uint8_t*)resp_completed, strlen(resp_completed));
    assert(rc == 0);
    assert(harness_response_status(ctx) == HARNESS_RESPONSE_COMPLETED);
    printf("  response_parse completed: OK\n");

    rc = harness_feed_input(ctx, (const uint8_t*)resp_tools, strlen(resp_tools));
    assert(rc == 0);
    assert(harness_response_status(ctx) == HARNESS_RESPONSE_REQUIRES_ACTION);
    assert(harness_response_tool_call_count(ctx) >= 1);
    printf("  response_parse requires_action: OK\n");

    rc = harness_message_append_tool_result(ctx, "call_abc", "{\"price\":190.2}");
    assert(rc == 0);

    rc = harness_honcho_mirror_message(ctx, "agent_devbot",
                                       "Migration script validation passed.",
                                       "{\"type\":\"agent_response\"}");
    assert(rc == 0);
    printf("  honcho_mirror (narrative): OK\n");

    rc = harness_log_interaction(ctx, "gpt-4o", "ctx", "resp");
    assert(rc == 0);

    assert(harness_should_loop(ctx, "max_loops < 5") == true);

    /* Ensure we can pull at least one event */
    drain_events(ctx);
    assert(harness_next_event(ctx, &ev) == 0);
    assert(ev.type == HARNESS_EVENT_NONE);

    harness_destroy(ctx);
    printf("  destroy: OK\n");
    printf("libharness smoke test PASSED\n");
    return 0;
}
