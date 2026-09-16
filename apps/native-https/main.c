/* SPDX-License-Identifier: GPL-3.0-only */
#include <opengat/package_fetch.h>
#include <opengat/runtime.h>

#include <stdio.h>

#include "trust_anchor.h"

#define HTTPS_PORT 443U
#define HTTPS_DEADLINE_NS UINT64_C(15000000000)
#define EXPECTED_BODY_BYTES 34U

static const uint8_t expected_sha256[32] = {
    0x7fU, 0xd7U, 0xccU, 0xdbU, 0xf5U, 0xc9U, 0x6bU, 0x38U,
    0x4fU, 0x62U, 0x7dU, 0x6fU, 0xcdU, 0xfaU, 0x50U, 0x3aU,
    0x36U, 0x9aU, 0x2cU, 0xd9U, 0xd1U, 0xd1U, 0x8eU, 0x9aU,
    0x05U, 0x2bU, 0x3aU, 0xd2U, 0xefU, 0x68U, 0x13U, 0xdaU
};

static uint64_t deadline(void)
{
    const uint64_t now = opengat_monotonic_ns();

    return now > UINT64_MAX - HTTPS_DEADLINE_NS ? UINT64_MAX :
        now + HTTPS_DEADLINE_NS;
}

int main(void)
{
    struct opengat_package_fetch_report report;
    const struct opengat_package_fetch_request request = {
        "repo.opengat.test", HTTPS_PORT, 0U, "/artifact.bin",
        opengat_https_test_anchors,
        sizeof(opengat_https_test_anchors) /
            sizeof(opengat_https_test_anchors[0]),
        deadline(), 128U, EXPECTED_BODY_BYTES, expected_sha256,
        "HTTPS.NEW", "HTTPS.TXT"
    };
    enum opengat_package_fetch_status status;

    puts("OPENGAT HTTPSAPP PHASE start");
    status = opengat_package_fetch_stage(&request, &report);
    if (status != OPENGAT_PACKAGE_FETCH_OK) {
        printf("OPENGAT HTTPSAPP REFUSED %s https=%s tls=%d transport=%ld "
            "storage=%ld cleanup=%ld\n",
            opengat_package_fetch_status_string(status),
            opengat_https_status_string(report.https_status),
            report.bearssl_error, report.transport_error,
            report.storage_error, report.cleanup_error);
        return 20;
    }
    if (report.bytes_received != EXPECTED_BODY_BYTES ||
        !report.published || !report.durable) {
        return 21;
    }
    puts("OPENGAT HTTPSAPP PHASE authenticated-download PASS");
    puts("OPENGAT HTTPSAPP PHASE durable-output PASS");
    const struct opengat_package_fetch_upload_request upload_request = {
        "repo.opengat.test", HTTPS_PORT, 0U, "/artifact.bin",
        opengat_https_test_anchors,
        sizeof(opengat_https_test_anchors) /
            sizeof(opengat_https_test_anchors[0]),
        deadline(), EXPECTED_BODY_BYTES, expected_sha256
    };

    status = opengat_package_fetch_upload(&upload_request, &report);
    if (status != OPENGAT_PACKAGE_FETCH_OK || !report.durable ||
        report.upload == OPENGAT_HANDLE_INVALID ||
        report.upload_flags != (OPENGAT_PACKAGE_UPLOAD_SEALED |
            OPENGAT_PACKAGE_UPLOAD_DURABLE)) {
        printf("OPENGAT HTTPSAPP UPLOAD REFUSED %s https=%s storage=%ld "
            "cleanup=%ld flags=%u\n",
            opengat_package_fetch_status_string(status),
            opengat_https_status_string(report.https_status),
            report.storage_error, report.cleanup_error, report.upload_flags);
        if (report.upload != OPENGAT_HANDLE_INVALID) {
            (void)opengat_package_upload_close(report.upload);
        }
        return 22;
    }
    if (opengat_package_upload_close(report.upload) < 0) {
        return 23;
    }
    puts("OPENGAT HTTPSAPP PHASE kernel-upload PASS");
    puts("OPENGAT HTTPSAPP PASS hostname time trust length close upload");
    return 0;
}
