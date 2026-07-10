/* Dialectic-style Honcho buffer tests (caller-owned HTTP; pure buffer handoff).
 * Agent A builds peer-card / conclude / messages request; Agent B parses JSON. */
#include "harness.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void drain(harness_ctx_t* ctx) {
    harness_event_t ev;
    while (harness_next_event(ctx, &ev) == 0 && ev.type != HARNESS_EVENT_NONE) {
    }
}

int main(void) {
    harness_config_t cfg_a;
    harness_config_t cfg_b;
    harness_ctx_t* a;
    harness_ctx_t* b;
    uint8_t out[4096];
    size_t out_len = 0;
    char meta[128];
    size_t mn = 0;
    harness_event_t ev;
    int saw_ready = 0;
    int saw_parsed = 0;
    int saw_detail = 0;

    printf("honcho dialectic buffer test...\n");
    harness_config_init_defaults(&cfg_a);
    harness_config_init_defaults(&cfg_b);
    cfg_a.workspace_id = "ws_demo";
    cfg_a.session_id = "sess_a";
    cfg_a.acting_peer_id = "agent_a";
    cfg_b.workspace_id = "ws_demo";
    cfg_b.session_id = "sess_b";
    cfg_b.acting_peer_id = "agent_b";

    a = harness_create_with_config(HARNESS_ROLE_MAIN, &cfg_a);
    b = harness_create_with_config(HARNESS_ROLE_MEMORY, &cfg_b);
    assert(a && b);
    assert(harness_participant_add(a, "human_alice", HARNESS_PARTICIPANT_HUMAN, true) == 0);
    assert(harness_participant_add(a, "agent_a", HARNESS_PARTICIPANT_AGENT, true) == 0);
    assert(harness_participant_add(b, "agent_b", HARNESS_PARTICIPANT_AGENT, true) == 0);

    /* peer-card feed path */
    assert(harness_honcho_feed_peer_card(a, "human_alice") == 0);
    assert(harness_get_output(a, out, sizeof(out) - 1, &out_len) == 0);
    out[out_len] = '\0';
    assert(strstr((char*)out, "get_peer_card") != NULL);
    assert(strstr((char*)out, "human_alice") != NULL);
    assert(strstr((char*)out, "ws_demo") != NULL);
    saw_ready = 0;
    saw_detail = 0;
    while (harness_next_event(a, &ev) == 0 && ev.type != HARNESS_EVENT_NONE) {
        if (ev.type == HARNESS_EVENT_HONCHO_REQUEST_READY) {
            saw_ready = 1;
            if (strcmp(ev.detail, "peer_card") == 0) saw_detail = 1;
            assert(strcmp(ev.peer_id, "human_alice") == 0);
        }
    }
    assert(saw_ready && saw_detail);
    printf("  feed peer_card + event detail: OK\n");

    /* conclude feed */
    assert(harness_honcho_feed_conclude(a, "human_alice", "prefers brevity") == 0);
    assert(harness_get_output(a, out, sizeof(out) - 1, &out_len) == 0);
    out[out_len] = '\0';
    assert(strstr((char*)out, "conclude") != NULL);
    assert(strstr((char*)out, "prefers brevity") != NULL);
    drain(a);
    printf("  feed conclude: OK\n");

    /* narrative messages request staged, then peer B parses a pretended response */
    assert(harness_honcho_metadata_agent(meta, sizeof(meta), &mn, "gpt-4o") == 0);
    assert(harness_honcho_build_messages_request(a, "agent_a",
        "Migration validation passed.", meta) == 0);
    assert(harness_get_output(a, out, sizeof(out) - 1, &out_len) == 0);
    out[out_len] = '\0';
    assert(strstr((char*)out, "\"messages\"") != NULL);
    assert(strstr((char*)out, "agent_a") != NULL);
    /* tool-like content rejected */
    assert(harness_honcho_build_messages_request(a, "agent_a",
        "{\"tool_calls\":[]}", NULL) != 0);

    {
        const char* honcho_reply =
            "{\"messages\":["
            "{\"peer_id\":\"human_alice\",\"content\":\"ack\"},"
            "{\"peer_id\":\"agent_b\",\"content\":\"noted\"}"
            "]}";
        assert(harness_honcho_parse_response(b, (const uint8_t*)honcho_reply,
                                             strlen(honcho_reply)) == 0);
        saw_parsed = 0;
        while (harness_next_event(b, &ev) == 0 && ev.type != HARNESS_EVENT_NONE) {
            if (ev.type == HARNESS_EVENT_HONCHO_RESPONSE_PARSED) {
                saw_parsed++;
                assert(ev.peer_id[0] != '\0');
            }
        }
        assert(saw_parsed >= 2);
    }
    printf("  messages request + peer parse: OK\n");

    /* memory KV survives as local facility */
    assert(harness_honcho_store_memory(a, "human_alice", "tone", "concise") == 0);
    assert(strcmp(harness_honcho_get_memory(a, "human_alice", "tone"), "concise") == 0);

    harness_destroy(a);
    harness_destroy(b);
    printf("honcho dialectic buffer test PASSED\n");
    return 0;
}
