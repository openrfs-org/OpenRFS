/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/cpu.h>
#include <openrfs/package_state.h>
#include <openrfs/random.h>

/*
 * HMAC_DRBG-SHA-256 follows SP 800-90A Rev. 1, section 10.1.2. The SHA-256
 * engine is the existing package_state implementation; this file does not
 * introduce another hash or AEAD. CPU entropy is a platform trust assumption,
 * not a min-entropy measurement made by QEMU.
 */
#define CPUID_BASIC UINT32_C(0)
#define CPUID_FEATURES UINT32_C(1)
#define CPUID_EXTENDED UINT32_C(7)
#define CPUID_RDRAND (UINT32_C(1) << 30U)
#define CPUID_RDSEED (UINT32_C(1) << 18U)
#define HARDWARE_ATTEMPTS 16U
#define SEED_SAMPLES 64U
#define SEED_RAW_BYTES (SEED_SAMPLES * 8U)
#define SEED_MATERIAL_BYTES 48U
#define DRBG_RESEED_INTERVAL UINT64_C(1048576)
#define HEALTH_RCT_CUTOFF 5U
#define HEALTH_APT_CUTOFF 8U
#define HEALTH_APT_WINDOW 512U

struct byte_span {
    const uint8_t *bytes;
    size_t length;
};

struct drbg_state {
    uint8_t key[32];
    uint8_t value[32];
    uint64_t reseed_counter;
    bool ready;
};

struct source_health {
    uint64_t last;
    uint64_t window_value;
    uint32_t repetitions;
    uint32_t window_count;
    uint32_t window_position;
    bool started;
};

static struct random_state state;
static struct drbg_state generator;
static struct source_health health;
static bool initialization_attempted;
#ifdef OPENRFS_RANDOM_TEST_SOURCE
static bool (*test_source)(uint64_t *word);
#endif

static void wipe(void *memory, size_t length)
{
    volatile uint8_t *bytes = memory;

    while (length != 0U) {
        *bytes++ = 0U;
        --length;
    }
}

static void copy_bytes(uint8_t *destination, const uint8_t *source, size_t length)
{
    for (size_t index = 0U; index < length; ++index) {
        destination[index] = source[index];
    }
}

static bool sha_update(struct package_state_sha256_context *context,
    const uint8_t *bytes, size_t length)
{
    return package_state_sha256_update(context, bytes, length) ==
        PACKAGE_STATE_STATUS_OK;
}

static bool hmac(const uint8_t *key, size_t key_length,
    const struct byte_span *parts, size_t part_count, uint8_t result[32])
{
    struct package_state_sha256_context context;
    uint8_t pad[64] = {0};
    uint8_t inner[32] = {0};
    bool ok = key != NULL && key_length <= sizeof(pad) &&
        result != NULL && part_count <= 3U;

    if (part_count != 0U && parts == NULL) {
        ok = false;
    }
    if (!ok) {
        return false;
    }
    for (size_t index = 0U; index < part_count; ++index) {
        if (parts[index].bytes == NULL && parts[index].length != 0U) {
            return false;
        }
    }
    copy_bytes(pad, key, key_length);
    for (size_t index = 0U; index < sizeof(pad); ++index) {
        pad[index] ^= UINT8_C(0x36);
    }
    ok = package_state_sha256_initialize(&context) == PACKAGE_STATE_STATUS_OK &&
        sha_update(&context, pad, sizeof(pad));
    for (size_t index = 0U; ok && index < part_count; ++index) {
        ok = sha_update(&context, parts[index].bytes, parts[index].length);
    }
    if (ok) {
        ok = package_state_sha256_finish(&context, inner) ==
            PACKAGE_STATE_STATUS_OK;
    }
    wipe(&context, sizeof(context));
    for (size_t index = 0U; index < sizeof(pad); ++index) {
        pad[index] ^= UINT8_C(0x36) ^ UINT8_C(0x5c);
    }
    if (ok) {
        ok = package_state_sha256_initialize(&context) ==
            PACKAGE_STATE_STATUS_OK &&
            sha_update(&context, pad, sizeof(pad)) &&
            sha_update(&context, inner, sizeof(inner)) &&
            package_state_sha256_finish(&context, result) ==
                PACKAGE_STATE_STATUS_OK;
    }
    wipe(&context, sizeof(context));
    wipe(pad, sizeof(pad));
    wipe(inner, sizeof(inner));
    return ok;
}

static bool hkdf_seed(const uint8_t raw[SEED_RAW_BYTES],
    uint8_t material[SEED_MATERIAL_BYTES])
{
    static const uint8_t salt[] = "OpenRFS/entropy-extract/v1";
    static const uint8_t label[] = "OpenRFS/DRBG-seed/v1";
    static const uint8_t one = 1U;
    static const uint8_t two = 2U;
    uint8_t prk[32] = {0};
    uint8_t first[32] = {0};
    uint8_t second[32] = {0};
    const struct byte_span input = {raw, SEED_RAW_BYTES};
    const struct byte_span first_parts[] = {
        {label, sizeof(label) - 1U}, {&one, 1U}
    };
    const struct byte_span second_parts[] = {
        {first, sizeof(first)}, {label, sizeof(label) - 1U}, {&two, 1U}
    };
    bool ok = hmac(salt, sizeof(salt) - 1U, &input, 1U, prk) &&
        hmac(prk, sizeof(prk), first_parts, 2U, first) &&
        hmac(prk, sizeof(prk), second_parts, 3U, second);

    if (ok) {
        copy_bytes(material, first, sizeof(first));
        copy_bytes(material + sizeof(first), second,
            SEED_MATERIAL_BYTES - sizeof(first));
    }
    wipe(prk, sizeof(prk));
    wipe(first, sizeof(first));
    wipe(second, sizeof(second));
    return ok;
}

static bool drbg_update(struct drbg_state *drbg, const uint8_t *data,
    size_t length)
{
    static const uint8_t zero = 0U;
    static const uint8_t one = 1U;
    const struct byte_span zero_parts[] = {
        {drbg->value, sizeof(drbg->value)}, {&zero, 1U}, {data, length}
    };
    const struct byte_span one_parts[] = {
        {drbg->value, sizeof(drbg->value)}, {&one, 1U}, {data, length}
    };

    if (!hmac(drbg->key, sizeof(drbg->key), zero_parts, 3U, drbg->key) ||
        !hmac(drbg->key, sizeof(drbg->key),
            (const struct byte_span[]){{drbg->value, sizeof(drbg->value)}},
            1U, drbg->value)) {
        return false;
    }
    if (data == NULL || length == 0U) {
        return true;
    }
    return hmac(drbg->key, sizeof(drbg->key), one_parts, 3U, drbg->key) &&
        hmac(drbg->key, sizeof(drbg->key),
            (const struct byte_span[]){{drbg->value, sizeof(drbg->value)}},
            1U, drbg->value);
}

static bool drbg_instantiate(struct drbg_state *drbg, const uint8_t *seed,
    size_t seed_length)
{
    if (drbg == NULL || seed == NULL || seed_length == 0U) {
        return false;
    }
    wipe(drbg, sizeof(*drbg));
    for (size_t index = 0U; index < sizeof(drbg->value); ++index) {
        drbg->value[index] = 1U;
    }
    if (!drbg_update(drbg, seed, seed_length)) {
        wipe(drbg, sizeof(*drbg));
        return false;
    }
    drbg->reseed_counter = 1U;
    drbg->ready = true;
    return true;
}

static bool drbg_generate(struct drbg_state *drbg, uint8_t *output,
    size_t length)
{
    size_t produced = 0U;

    if (drbg == NULL || !drbg->ready || output == NULL ||
        length > RANDOM_MAX_REQUEST_BYTES ||
        drbg->reseed_counter > DRBG_RESEED_INTERVAL) {
        return false;
    }
    while (produced < length) {
        size_t chunk = length - produced;
        const struct byte_span value = {drbg->value, sizeof(drbg->value)};

        if (!hmac(drbg->key, sizeof(drbg->key), &value, 1U, drbg->value)) {
            return false;
        }
        if (chunk > sizeof(drbg->value)) {
            chunk = sizeof(drbg->value);
        }
        copy_bytes(output + produced, drbg->value, chunk);
        produced += chunk;
    }
    if (!drbg_update(drbg, NULL, 0U)) {
        return false;
    }
    ++drbg->reseed_counter;
    return true;
}

static bool health_sample(uint64_t word)
{
    if (!health.started) {
        health.last = word;
        health.window_value = word;
        health.repetitions = 1U;
        health.window_count = 1U;
        health.window_position = 1U;
        health.started = true;
        return true;
    }
    health.repetitions = word == health.last ? health.repetitions + 1U : 1U;
    health.last = word;
    if (health.repetitions >= HEALTH_RCT_CUTOFF) {
        return false;
    }
    if (health.window_position >= HEALTH_APT_WINDOW) {
        health.window_value = word;
        health.window_count = 1U;
        health.window_position = 1U;
        return true;
    }
    ++health.window_position;
    if (word == health.window_value &&
        ++health.window_count >= HEALTH_APT_CUTOFF) {
        return false;
    }
    return true;
}

static bool hardware_word(uint64_t *word)
{
    unsigned char success = 0U;

#ifdef OPENRFS_RANDOM_TEST_SOURCE
    if (test_source != NULL) {
        return test_source(word);
    }
#endif
    for (size_t attempt = 0U; attempt < HARDWARE_ATTEMPTS; ++attempt) {
        if (state.rdseed) {
            __asm__ volatile ("rdseed %0; setc %1"
                : "=r" (*word), "=qm" (success) : : "cc");
        } else if (state.rdrand) {
            __asm__ volatile ("rdrand %0; setc %1"
                : "=r" (*word), "=qm" (success) : : "cc");
        } else {
            return false;
        }
        if (success != 0U) {
            return true;
        }
    }
    return false;
}

static void lockout(void)
{
    wipe(&generator, sizeof(generator));
    state.capability = RANDOM_CAPABILITY_FAILED;
}

static bool collect_seed(uint8_t material[SEED_MATERIAL_BYTES])
{
    uint8_t raw[SEED_RAW_BYTES] = {0};
    bool ok = true;

    for (size_t index = 0U; index < SEED_SAMPLES; ++index) {
        uint64_t word = 0U;

        if (!hardware_word(&word) || !health_sample(word)) {
            ok = false;
            break;
        }
        for (size_t byte = 0U; byte < sizeof(word); ++byte) {
            raw[index * sizeof(word) + byte] =
                (uint8_t)(word >> (byte * 8U));
        }
        word = 0U;
    }
    if (ok) {
        ok = hkdf_seed(raw, material);
    }
    wipe(raw, sizeof(raw));
    return ok;
}

void random_initialize(void)
{
    struct cpuid_result basic = {0};
    struct cpuid_result features = {0};
    struct cpuid_result extended = {0};
    uint8_t material[SEED_MATERIAL_BYTES] = {0};

    if (initialization_attempted) {
        return;
    }
    initialization_attempted = true;
    cpu_cpuid(CPUID_BASIC, 0U, &basic);
    if (basic.eax >= CPUID_FEATURES) {
        cpu_cpuid(CPUID_FEATURES, 0U, &features);
    }
    if (basic.eax >= CPUID_EXTENDED) {
        cpu_cpuid(CPUID_EXTENDED, 0U, &extended);
    }
    state.rdrand = (features.ecx & CPUID_RDRAND) != 0U;
    state.rdseed = (extended.ebx & CPUID_RDSEED) != 0U;
    if (!state.rdseed && !state.rdrand) {
        state.capability = RANDOM_CAPABILITY_UNAVAILABLE;
        return;
    }
    if (!collect_seed(material) ||
        !drbg_instantiate(&generator, material, sizeof(material))) {
        lockout();
    } else {
        state.capability = RANDOM_CAPABILITY_INITIALIZED;
        state.reseed_count = 1U;
    }
    wipe(material, sizeof(material));
}

static enum random_status serve(void *destination, size_t length)
{
    uint8_t staging[RANDOM_MAX_REQUEST_BYTES] = {0};
    uint8_t material[SEED_MATERIAL_BYTES] = {0};
    const bool restore_interrupts = cpu_interrupts_enabled();
    enum random_status status = RANDOM_STATUS_OK;

    if (destination == NULL && length != 0U) {
        return RANDOM_STATUS_NULL_ARGUMENT;
    }
    if (length > RANDOM_MAX_REQUEST_BYTES) {
        return RANDOM_STATUS_TOO_LARGE;
    }
    if (restore_interrupts) {
        cpu_interrupt_disable();
    }
    if (state.capability == RANDOM_CAPABILITY_UNAVAILABLE) {
        status = RANDOM_STATUS_NOT_INITIALIZED;
    } else if (state.capability != RANDOM_CAPABILITY_INITIALIZED) {
        status = RANDOM_STATUS_SOURCE_FAILED;
    } else if (length != 0U) {
        if (generator.reseed_counter > DRBG_RESEED_INTERVAL) {
            if (!collect_seed(material) ||
                !drbg_update(&generator, material, sizeof(material))) {
                lockout();
                status = RANDOM_STATUS_RESEED_REQUIRED;
            } else {
                generator.reseed_counter = 1U;
                ++state.reseed_count;
            }
        }
        if (status == RANDOM_STATUS_OK &&
            !drbg_generate(&generator, staging, length)) {
            lockout();
            status = RANDOM_STATUS_SOURCE_FAILED;
        }
        if (status == RANDOM_STATUS_OK) {
            copy_bytes(destination, staging, length);
            if (UINT64_MAX - state.bytes_issued < length) {
                state.bytes_issued = UINT64_MAX;
            } else {
                state.bytes_issued += length;
            }
        }
    }
    wipe(staging, sizeof(staging));
    wipe(material, sizeof(material));
    if (restore_interrupts) {
        cpu_interrupt_enable();
    }
    return status;
}

enum random_status random_bytes(void *destination, size_t length)
{
    return serve(destination, length);
}

enum random_status random_strong_bytes(void *destination, size_t length)
{
    return serve(destination, length);
}

struct random_state random_get_state(void)
{
    const bool restore_interrupts = cpu_interrupts_enabled();
    struct random_state snapshot;

    if (restore_interrupts) {
        cpu_interrupt_disable();
    }
    snapshot = state;
    if (restore_interrupts) {
        cpu_interrupt_enable();
    }
    return snapshot;
}

static bool equal_bytes(const uint8_t *left, const uint8_t *right, size_t length)
{
    uint8_t difference = 0U;

    for (size_t index = 0U; index < length; ++index) {
        difference |= left[index] ^ right[index];
    }
    return difference == 0U;
}

bool random_self_test(void)
{
    static const uint8_t key[20] = {
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b
    };
    static const uint8_t expected_hmac[32] = {
        0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53,
        0x5c, 0xa8, 0xaf, 0xce, 0xaf, 0x0b, 0xf1, 0x2b,
        0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7,
        0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7
    };
    static const uint8_t message[] = "Hi There";
    static const uint8_t seed[48] = {
        0xca, 0x85, 0x19, 0x11, 0x34, 0x93, 0x84, 0xbf,
        0xfe, 0x89, 0xde, 0x1c, 0xbd, 0xc4, 0x6e, 0x68,
        0x31, 0xe4, 0x4d, 0x34, 0xa4, 0xfb, 0x93, 0x5e,
        0xe2, 0x85, 0xdd, 0x14, 0xb7, 0x1a, 0x74, 0x88,
        0x65, 0x9b, 0xa9, 0x6c, 0x60, 0x1d, 0xc6, 0x9f,
        0xc9, 0x02, 0x94, 0x08, 0x05, 0xec, 0x0c, 0xa8
    };
    static const uint8_t expected_drbg[32] = {
        0xe5, 0x28, 0xe9, 0xab, 0xf2, 0xde, 0xce, 0x54,
        0xd4, 0x7c, 0x7e, 0x75, 0xe5, 0xfe, 0x30, 0x21,
        0x49, 0xf8, 0x17, 0xea, 0x9f, 0xb4, 0xbe, 0xe6,
        0xf4, 0x19, 0x96, 0x97, 0xd0, 0x4d, 0x5b, 0x89
    };
    const struct byte_span input = {message, sizeof(message) - 1U};
    struct drbg_state test = {0};
    uint8_t tag[32] = {0};
    uint8_t output[128] = {0};
    bool ok = hmac(key, sizeof(key), &input, 1U, tag) &&
        equal_bytes(tag, expected_hmac, sizeof(tag)) &&
        drbg_instantiate(&test, seed, sizeof(seed)) &&
        drbg_generate(&test, output, sizeof(output)) &&
        drbg_generate(&test, output, sizeof(output)) &&
        equal_bytes(output, expected_drbg, sizeof(expected_drbg)) &&
        random_bytes(NULL, 1U) == RANDOM_STATUS_NULL_ARGUMENT &&
        random_bytes(tag, RANDOM_MAX_REQUEST_BYTES + 1U) ==
            RANDOM_STATUS_TOO_LARGE;

    wipe(&test, sizeof(test));
    wipe(tag, sizeof(tag));
    wipe(output, sizeof(output));
    return ok;
}

const char *random_capability_string(enum random_capability capability)
{
    switch (capability) {
    case RANDOM_CAPABILITY_UNAVAILABLE: return "unavailable";
    case RANDOM_CAPABILITY_DEGRADED: return "degraded (refusing)";
    case RANDOM_CAPABILITY_INITIALIZED: return "initialized";
    case RANDOM_CAPABILITY_FAILED: return "source failed (locked out)";
    default: return "unknown";
    }
}

const char *random_status_string(enum random_status status)
{
    static const char *const messages[RANDOM_STATUS_COUNT] = {
        "ok",
        "null random destination",
        "random request exceeds the bounded limit",
        "random source is not initialized",
        "strong hardware entropy is unavailable",
        "random source failed its health check",
        "random source could not reseed"
    };

    _Static_assert(sizeof(messages) / sizeof(messages[0]) ==
        RANDOM_STATUS_COUNT, "random status messages are out of sync");
    if (status < RANDOM_STATUS_OK || status >= RANDOM_STATUS_COUNT) {
        return "unknown random status";
    }
    return messages[status];
}
