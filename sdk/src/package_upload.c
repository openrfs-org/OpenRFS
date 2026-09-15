/* SPDX-License-Identifier: GPL-3.0-only */
#include <trait/package_upload.h>

#include <trait/runtime.h>
#include <string.h>

long trait_package_upload_open(void)
{
    return trait_syscall0(TRAIT_SYS_PACKAGE_UPLOAD_OPEN);
}

long trait_package_upload_write(
    trait_handle_t upload,
    const void *bytes,
    size_t byte_count
)
{
    const struct trait_package_upload_write_request request = {
        sizeof(request), TRAIT_ABI_VERSION, upload,
        (uint64_t)(uintptr_t)bytes, (uint32_t)byte_count, 0U
    };

    if ((bytes == NULL && byte_count != 0U) ||
        byte_count > TRAIT_PACKAGE_UPLOAD_WRITE_MAX) {
        return -TRAIT_EINVAL;
    }
    return trait_syscall1(TRAIT_SYS_PACKAGE_UPLOAD_WRITE,
        (uint64_t)(uintptr_t)&request);
}

long trait_package_upload_seal(
    trait_handle_t upload,
    uint64_t expected_bytes,
    const uint8_t expected_sha256[TRAIT_PACKAGE_UPLOAD_SHA256_BYTES],
    struct trait_package_upload_report *report
)
{
    struct trait_package_upload_seal_request request;

    if (expected_sha256 == NULL || report == NULL || expected_bytes == 0U ||
        expected_bytes > TRAIT_PACKAGE_UPLOAD_MAX_BYTES) {
        return -TRAIT_EINVAL;
    }
    (void)memset(&request, 0, sizeof(request));
    request.size = sizeof(request);
    request.version = TRAIT_ABI_VERSION;
    request.handle = upload;
    request.expected_bytes = expected_bytes;
    (void)memcpy(request.expected_sha256, expected_sha256,
        sizeof(request.expected_sha256));
    long status = trait_syscall1(TRAIT_SYS_PACKAGE_UPLOAD_SEAL,
        (uint64_t)(uintptr_t)&request);

    report->actual_bytes = request.actual_bytes;
    (void)memcpy(report->actual_sha256, request.actual_sha256,
        sizeof(report->actual_sha256));
    report->result_flags = request.result_flags;
    return status;
}

long trait_package_upload_close(trait_handle_t upload)
{
    return trait_handle_close(upload);
}
