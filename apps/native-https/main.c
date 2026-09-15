/* SPDX-License-Identifier: GPL-3.0-only */
#include <trait/package_fetch.h>
#include <trait/runtime.h>

#include <stdio.h>

#include "trust_anchor.h"

#define HTTPS_PORT 443U
#define HTTPS_DEADLINE_NS UINT64_C(15000000000)
#define EXPECTED_BODY_BYTES 35U

static const uint8_t expected_sha256[32] = {
    0x30U, 0x21U, 0x81U, 0x18U, 0x60U, 0xe7U, 0xe5U, 0xb2U,
    0xc6U, 0xbfU, 0xc1U, 0x51U, 0x6dU, 0x59U, 0x90U, 0x20U,
    0xdfU, 0x58U, 0x99U, 0x07U, 0x70U, 0x8cU, 0x76U, 0x3bU,
    0x8bU, 0xb5U, 0xdfU, 0x3aU, 0xd4U, 0xaeU, 0x4dU, 0x07U
};

static uint64_t deadline(void)
{
    const uint64_t now = trait_monotonic_ns();

    return now > UINT64_MAX - HTTPS_DEADLINE_NS ? UINT64_MAX :
        now + HTTPS_DEADLINE_NS;
}

int main(void)
{
    struct trait_package_fetch_report report;
    const struct trait_package_fetch_request request = {
        "repo.trait.test", HTTPS_PORT, 0U, "/artifact.bin",
        trait_https_test_anchors,
        sizeof(trait_https_test_anchors) /
            sizeof(trait_https_test_anchors[0]),
        deadline(), 128U, EXPECTED_BODY_BYTES, expected_sha256,
        "HTTPS.NEW", "HTTPS.TXT"
    };
    enum trait_package_fetch_status status;

    puts("TRAIT HTTPSAPP PHASE start");
    status = trait_package_fetch_stage(&request, &report);
    if (status != TRAIT_PACKAGE_FETCH_OK) {
        printf("TRAIT HTTPSAPP REFUSED %s https=%s tls=%d transport=%ld "
            "storage=%ld cleanup=%ld\n",
            trait_package_fetch_status_string(status),
            trait_https_status_string(report.https_status),
            report.bearssl_error, report.transport_error,
            report.storage_error, report.cleanup_error);
        return 20;
    }
    if (report.bytes_received != EXPECTED_BODY_BYTES ||
        !report.published || !report.durable) {
        return 21;
    }
    puts("TRAIT HTTPSAPP PHASE authenticated-download PASS");
    puts("TRAIT HTTPSAPP PHASE durable-output PASS");
    const struct trait_package_fetch_upload_request upload_request = {
        "repo.trait.test", HTTPS_PORT, 0U, "/artifact.bin",
        trait_https_test_anchors,
        sizeof(trait_https_test_anchors) /
            sizeof(trait_https_test_anchors[0]),
        deadline(), EXPECTED_BODY_BYTES, expected_sha256
    };

    status = trait_package_fetch_upload(&upload_request, &report);
    if (status != TRAIT_PACKAGE_FETCH_OK || !report.durable ||
        report.upload == TRAIT_HANDLE_INVALID ||
        report.upload_flags != (TRAIT_PACKAGE_UPLOAD_SEALED |
            TRAIT_PACKAGE_UPLOAD_DURABLE)) {
        printf("TRAIT HTTPSAPP UPLOAD REFUSED %s https=%s storage=%ld "
            "cleanup=%ld flags=%u\n",
            trait_package_fetch_status_string(status),
            trait_https_status_string(report.https_status),
            report.storage_error, report.cleanup_error, report.upload_flags);
        if (report.upload != TRAIT_HANDLE_INVALID) {
            (void)trait_package_upload_close(report.upload);
        }
        return 22;
    }
    if (trait_package_upload_close(report.upload) < 0) {
        return 23;
    }
    puts("TRAIT HTTPSAPP PHASE kernel-upload PASS");
    puts("TRAIT HTTPSAPP PASS hostname time trust length close upload");
    return 0;
}
