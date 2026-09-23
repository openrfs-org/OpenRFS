/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <openrfs/cpu.h>

static bool fake_available;
static bool fake_repeat;
static bool fake_fail;
static uint64_t fake_next;

void cpu_cpuid(uint32_t leaf, uint32_t subleaf, struct cpuid_result *result)
{
    (void)subleaf;
    result->eax = leaf == 0U && fake_available ? 7U : 0U;
    result->ebx = 0U;
    result->ecx = 0U;
    result->edx = 0U;
    if (leaf == 7U && fake_available) {
        result->ebx = UINT32_C(1) << 18U;
    }
}

bool cpu_interrupts_enabled(void)
{
    return false;
}

void cpu_interrupt_disable(void) {}
void cpu_interrupt_enable(void) {}

/* Include the production entry point so hardware health failures can be
 * injected without exposing an entropy injection API to the kernel. */
#define OPENRFS_RANDOM_TEST_SOURCE 1
#include "../src/kernel/random.c"

static bool sample(uint64_t *word)
{
    if (fake_fail) {
        return false;
    }
    *word = fake_repeat ? UINT64_C(0x12345678) : ++fake_next;
    return true;
}

static void reset_for_test(void)
{
    wipe(&state, sizeof(state));
    wipe(&generator, sizeof(generator));
    wipe(&health, sizeof(health));
    initialization_attempted = false;
    fake_next = 0U;
    fake_repeat = false;
    fake_fail = false;
}

int main(void)
{
    static const uint8_t expected_seed[SEED_MATERIAL_BYTES] = {
        0x18, 0xe4, 0xa2, 0xe5, 0xa9, 0x56, 0xa3, 0x70,
        0xb7, 0x71, 0x73, 0xea, 0xa0, 0x54, 0x6d, 0xaa,
        0xa7, 0x4b, 0x86, 0xf1, 0xa1, 0xb5, 0x38, 0x94,
        0x06, 0x7d, 0x8e, 0xc8, 0x77, 0xf0, 0x96, 0x2e,
        0x75, 0x18, 0x29, 0xed, 0xd8, 0x40, 0xc9, 0x68,
        0x08, 0x48, 0x3c, 0x82, 0x34, 0xfb, 0x1f, 0xe3
    };
    uint8_t raw[SEED_RAW_BYTES];
    uint8_t material[SEED_MATERIAL_BYTES];
    uint8_t output[16] = {0};

    if (!random_self_test()) {
        fprintf(stderr, "RFC 4231 / NIST CAVP vectors failed\n");
        return 1;
    }
    for (size_t index = 0U; index < sizeof(raw); ++index) {
        raw[index] = (uint8_t)index;
    }
    /* Expected bytes were computed with Python's independent hmac/hashlib. */
    if (!hkdf_seed(raw, material) ||
        !equal_bytes(material, expected_seed, sizeof(material))) {
        fprintf(stderr, "HKDF seed differential failed\n");
        return 1;
    }
    wipe(raw, sizeof(raw));
    wipe(material, sizeof(material));
    random_initialize();
    if (random_get_state().capability != RANDOM_CAPABILITY_UNAVAILABLE ||
        random_bytes(output, sizeof(output)) !=
            RANDOM_STATUS_NOT_INITIALIZED) {
        fprintf(stderr, "missing entropy was not refused\n");
        return 1;
    }
    for (size_t index = 0U; index < sizeof(output); ++index) {
        if (output[index] != 0U) {
            fprintf(stderr, "refused request changed destination\n");
            return 1;
        }
    }
    wipe(&health, sizeof(health));
    for (uint32_t index = 0U; index < HEALTH_RCT_CUTOFF - 1U; ++index) {
        if (!health_sample(UINT64_C(0x12345678))) {
            fprintf(stderr, "repetition test refused early\n");
            return 1;
        }
    }
    if (health_sample(UINT64_C(0x12345678))) {
        fprintf(stderr, "repetition test missed a stuck source\n");
        return 1;
    }
    reset_for_test();
    fake_available = true;
    test_source = sample;
    random_initialize();
    if (random_get_state().capability != RANDOM_CAPABILITY_INITIALIZED ||
        random_strong_bytes(output, sizeof(output)) != RANDOM_STATUS_OK) {
        fprintf(stderr, "health-passing source did not initialize\n");
        return 1;
    }
    generator.reseed_counter = DRBG_RESEED_INTERVAL + 1U;
    fake_repeat = true;
    for (size_t index = 0U; index < sizeof(output); ++index) {
        output[index] = 0xA5U;
    }
    if (random_bytes(output, sizeof(output)) !=
            RANDOM_STATUS_RESEED_REQUIRED ||
        random_get_state().capability != RANDOM_CAPABILITY_FAILED ||
        random_bytes(output, sizeof(output)) != RANDOM_STATUS_SOURCE_FAILED) {
        fprintf(stderr, "failed reseed did not lock out the DRBG\n");
        return 1;
    }
    for (size_t index = 0U; index < sizeof(output); ++index) {
        if (output[index] != 0xA5U) {
            fprintf(stderr, "failed reseed changed destination\n");
            return 1;
        }
    }
    reset_for_test();
    fake_repeat = true;
    random_initialize();
    if (random_get_state().capability != RANDOM_CAPABILITY_FAILED) {
        fprintf(stderr, "stuck boot source was accepted\n");
        return 1;
    }
    reset_for_test();
    fake_fail = true;
    random_initialize();
    if (random_get_state().capability != RANDOM_CAPABILITY_FAILED) {
        fprintf(stderr, "failed boot source was accepted\n");
        return 1;
    }
    puts("random vectors, no entropy, failed source, and lockout passed");
    return 0;
}
