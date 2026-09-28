/* SPDX-License-Identifier: GPL-3.0-only */
#include <rsd/package_fetch.h>
#include <rsd/runtime.h>

#include <stdio.h>

#include "trust_anchor.h"

#define HTTPS_PORT 443U
#define HTTPS_DEADLINE_NS UINT64_C(15000000000)
#define EXPECTED_BODY_BYTES 34U

static const uint8_t expected_sha256[32] = {
    0x7bU, 0xe1U, 0x0fU, 0x3aU, 0x66U, 0x4eU, 0xd0U, 0x40U,
    0xacU, 0x30U, 0xbcU, 0x9eU, 0xf2U, 0x95U, 0xe1U, 0xf0U,
    0xa4U, 0xd0U, 0x4dU, 0x73U, 0x8fU, 0x58U, 0xd5U, 0x59U,
    0xd4U, 0x80U, 0x67U, 0x29U, 0xa3U, 0x6bU, 0x5cU, 0x76U
};

static uint64_t deadline(void)
{
    const uint64_t now = rsd_monotonic_ns();

    return now > UINT64_MAX - HTTPS_DEADLINE_NS ? UINT64_MAX :
        now + HTTPS_DEADLINE_NS;
}

int main(void)
{
    struct rsd_package_fetch_report report;
    const struct rsd_package_fetch_request request = {
        "repo.rsd.test", HTTPS_PORT, 0U, "/artifact.bin",
        rsd_https_test_anchors,
        sizeof(rsd_https_test_anchors) /
            sizeof(rsd_https_test_anchors[0]),
        deadline(), 128U, EXPECTED_BODY_BYTES, expected_sha256,
        "HTTPS.NEW", "HTTPS.TXT"
    };
    enum rsd_package_fetch_status status;

    puts("RSD HTTPSAPP PHASE start");
    status = rsd_package_fetch_stage(&request, &report);
    if (status != RSD_PACKAGE_FETCH_OK) {
        printf("RSD HTTPSAPP REFUSED %s https=%s tls=%d transport=%ld "
            "storage=%ld cleanup=%ld\n",
            rsd_package_fetch_status_string(status),
            rsd_https_status_string(report.https_status),
            report.bearssl_error, report.transport_error,
            report.storage_error, report.cleanup_error);
        return 20;
    }
    if (report.bytes_received != EXPECTED_BODY_BYTES ||
        !report.published || !report.durable) {
        return 21;
    }
    puts("RSD HTTPSAPP PHASE authenticated-download PASS");
    puts("RSD HTTPSAPP PHASE durable-output PASS");
    const struct rsd_package_fetch_upload_request upload_request = {
        "repo.rsd.test", HTTPS_PORT, 0U, "/artifact.bin",
        rsd_https_test_anchors,
        sizeof(rsd_https_test_anchors) /
            sizeof(rsd_https_test_anchors[0]),
        deadline(), EXPECTED_BODY_BYTES, expected_sha256
    };

    status = rsd_package_fetch_upload(&upload_request, &report);
    if (status != RSD_PACKAGE_FETCH_OK || !report.durable ||
        report.upload == RSD_HANDLE_INVALID ||
        report.upload_flags != (RSD_PACKAGE_UPLOAD_SEALED |
            RSD_PACKAGE_UPLOAD_DURABLE)) {
        printf("RSD HTTPSAPP UPLOAD REFUSED %s https=%s storage=%ld "
            "cleanup=%ld flags=%u\n",
            rsd_package_fetch_status_string(status),
            rsd_https_status_string(report.https_status),
            report.storage_error, report.cleanup_error, report.upload_flags);
        if (report.upload != RSD_HANDLE_INVALID) {
            (void)rsd_package_upload_close(report.upload);
        }
        return 22;
    }
    if (rsd_package_upload_close(report.upload) < 0) {
        return 23;
    }
    puts("RSD HTTPSAPP PHASE kernel-upload PASS");
    puts("RSD HTTPSAPP PASS hostname time trust length close upload");
    return 0;
}
