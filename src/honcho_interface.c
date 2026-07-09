/* Honcho workspace/peer/session/message surface (ADR 002).
 * Optional at deploy; peer ids remain first-class without a live Honcho.
 * Tool calls are not mirrored by default — use harness_honcho_mirror_message
 * only for narrative chat / agent responses.
 *
 * Public memory helpers are implemented in harness.c. This translation unit
 * owns optional handle attachment for callers that already hold a Honcho ctx.
 */
#include "harness_internal.h"

int harness_honcho_attach(harness_ctx_t* ctx, void* honcho_handle) {
    if (!ctx) return -1;
    ctx->honcho = honcho_handle;
    return 0;
}
