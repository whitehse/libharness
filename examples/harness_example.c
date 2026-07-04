/* harness_example.c - CC0 example (ADR 008 c-only-examples) */
#include "harness.h"
#include <stdio.h>

int main(void) {
    harness_ctx_t* ctx = harness_create(HARNESS_ROLE_MAIN);
    if (!ctx) return 1;
    printf("harness v%s created\n", harness_version());
    harness_destroy(ctx);
    return 0;
}