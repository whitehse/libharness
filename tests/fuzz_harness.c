/* LibFuzzer entry for response_parse + context message content.
 * Built only with -DENABLE_FUZZ=ON.
 */
#include "harness.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    harness_ctx_t* ctx = harness_create(HARNESS_ROLE_MAIN);
    harness_context_params_t params;
    if (!ctx) return 0;

    (void)harness_response_parse(ctx, data, size);

    if (size > 0 && size < 2048) {
        char tmp[2049];
        memcpy(tmp, data, size);
        tmp[size] = '\0';
        (void)harness_message_append(ctx, "fuzz_peer", HARNESS_MSG_USER, tmp, false);
        memset(&params, 0, sizeof(params));
        params.model = "fuzz";
        params.temperature = 0;
        params.include_tools = false;
        params.identity_prefix = true;
        params.redact_secrets = true;
        (void)harness_context_build(ctx, &params);
    }

    harness_destroy(ctx);
    return 0;
}
