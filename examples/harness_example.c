/* harness_example.c - CC0 example (ADR 008 c-only-examples)
 * Minimal multi-party session + context build (ADR 002).
 */
#include "harness.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    harness_config_t cfg;
    harness_ctx_t* ctx;
    harness_context_params_t params;
    uint8_t out[2048];
    size_t out_len = 0;

    harness_config_init_defaults(&cfg);
    cfg.workspace_id = "example_ws";
    cfg.session_id = "example_sess";
    cfg.acting_peer_id = "agent_helper";

    ctx = harness_create_with_config(HARNESS_ROLE_MAIN, &cfg);
    if (!ctx) return 1;

    printf("harness v%s\n", harness_version());

    (void)harness_participant_add(ctx, "human_alice", HARNESS_PARTICIPANT_HUMAN, true);
    (void)harness_participant_add(ctx, "agent_helper", HARNESS_PARTICIPANT_AGENT, true);
    (void)harness_soul_set(ctx, "Be concise and accurate.");
    (void)harness_message_append(ctx, "human_alice", HARNESS_MSG_USER,
                                 "Summarize today's risks.", false);

    memset(&params, 0, sizeof(params));
    params.model = "gpt-4o-mini";
    params.temperature = 0.0;
    params.include_tools = false;
    params.identity_prefix = true;
    params.redact_secrets = true;

    if (harness_context_build(ctx, &params) != 0) {
        harness_destroy(ctx);
        return 1;
    }
    if (harness_get_output(ctx, out, sizeof(out) - 1, &out_len) == 0) {
        out[out_len] = '\0';
        printf("context (%zu bytes): %s\n", out_len, (char*)out);
    }

    harness_destroy(ctx);
    return 0;
}
