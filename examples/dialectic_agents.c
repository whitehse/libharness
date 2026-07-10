/* dialectic_agents.c - CC0 example: two harness contexts exchange narrative.
 * No network — pure buffer handoff (ADR 004 spirit).
 */
#include "harness.h"
#include <stdio.h>
#include <string.h>

static void drain(harness_ctx_t* ctx) {
    harness_event_t ev;
    while (harness_next_event(ctx, &ev) == 0 && ev.type != HARNESS_EVENT_NONE) {
    }
}

int main(void) {
    harness_config_t cfg_a, cfg_b;
    harness_ctx_t *a, *b;
    harness_context_params_t params;
    uint8_t out[4096];
    size_t out_len = 0;
    char meta[128];
    size_t mn = 0;

    harness_config_init_defaults(&cfg_a);
    harness_config_init_defaults(&cfg_b);
    cfg_a.session_id = cfg_b.session_id = "sess_pair";
    cfg_a.workspace_id = cfg_b.workspace_id = "ws_pair";
    cfg_a.acting_peer_id = "agent_alpha";
    cfg_b.acting_peer_id = "agent_beta";

    a = harness_create_with_config(HARNESS_ROLE_MAIN, &cfg_a);
    b = harness_create_with_config(HARNESS_ROLE_MAIN, &cfg_b);
    if (!a || !b) return 1;

    (void)harness_participant_add(a, "agent_alpha", HARNESS_PARTICIPANT_AGENT, true);
    (void)harness_participant_add(a, "agent_beta", HARNESS_PARTICIPANT_AGENT, true);
    (void)harness_participant_add(b, "agent_alpha", HARNESS_PARTICIPANT_AGENT, true);
    (void)harness_participant_add(b, "agent_beta", HARNESS_PARTICIPANT_AGENT, true);

    (void)harness_soul_set(a, "You are Alpha, concise.");
    (void)harness_soul_set(b, "You are Beta, careful.");

    (void)harness_message_append(a, "agent_alpha", HARNESS_MSG_USER,
                                 "Propose a deploy window.", false);

    memset(&params, 0, sizeof(params));
    params.model = "gpt-4o-mini";
    params.temperature = 0;
    params.include_tools = false;
    params.identity_prefix = true;
    params.redact_secrets = true;

    if (harness_context_build(a, &params) != 0) return 1;
    (void)harness_get_output(a, out, sizeof(out) - 1, &out_len);
    out[out_len] = '\0';
    printf("alpha context: %s\n", (char*)out);

    /* Simulate model completion for Alpha, hand narrative to Beta */
    (void)harness_message_append(a, "agent_alpha", HARNESS_MSG_ASSISTANT,
                                 "Suggest 02:00 UTC low traffic.", false);
    (void)harness_message_append(b, "agent_alpha", HARNESS_MSG_ASSISTANT,
                                 "Suggest 02:00 UTC low traffic.", false);
    (void)harness_message_append(b, "agent_beta", HARNESS_MSG_USER,
                                 "Ack; I will validate backups first.", false);

    if (harness_context_build(b, &params) != 0) return 1;
    (void)harness_get_output(b, out, sizeof(out) - 1, &out_len);
    out[out_len] = '\0';
    printf("beta context: %s\n", (char*)out);

    (void)harness_honcho_metadata_agent(meta, sizeof(meta), &mn, "gpt-4o-mini");
    (void)harness_honcho_build_messages_request(b, "agent_beta",
        "Ack; I will validate backups first.", meta);
    (void)harness_get_output(b, out, sizeof(out) - 1, &out_len);
    out[out_len] = '\0';
    printf("honcho request: %s\n", (char*)out);

    drain(a);
    drain(b);
    harness_destroy(a);
    harness_destroy(b);
    printf("dialectic_agents example OK (harness %s)\n", harness_version());
    return 0;
}
