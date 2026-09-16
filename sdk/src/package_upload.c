/* SPDX-License-Identifier: GPL-3.0-only */
#include <opengat/package_upload.h>

#include <opengat/runtime.h>
#include <string.h>

long opengat_package_upload_open(void)
{
    return opengat_syscall0(OPENGAT_SYS_PACKAGE_UPLOAD_OPEN);
}

long opengat_package_upload_write(
    opengat_handle_t upload,
    const void *bytes,
    size_t byte_count
)
{
    const struct opengat_package_upload_write_request request = {
        sizeof(request), OPENGAT_ABI_VERSION, upload,
        (uint64_t)(uintptr_t)bytes, (uint32_t)byte_count, 0U
    };

    if ((bytes == NULL && byte_count != 0U) ||
        byte_count > OPENGAT_PACKAGE_UPLOAD_WRITE_MAX) {
        return -OPENGAT_EINVAL;
    }
    return opengat_syscall1(OPENGAT_SYS_PACKAGE_UPLOAD_WRITE,
        (uint64_t)(uintptr_t)&request);
}

long opengat_package_upload_seal(
    opengat_handle_t upload,
    uint64_t expected_bytes,
    const uint8_t expected_sha256[OPENGAT_PACKAGE_UPLOAD_SHA256_BYTES],
    struct opengat_package_upload_report *report
)
{
    struct opengat_package_upload_seal_request request;

    if (expected_sha256 == NULL || report == NULL || expected_bytes == 0U ||
        expected_bytes > OPENGAT_PACKAGE_UPLOAD_MAX_BYTES) {
        return -OPENGAT_EINVAL;
    }
    (void)memset(&request, 0, sizeof(request));
    request.size = sizeof(request);
    request.version = OPENGAT_ABI_VERSION;
    request.handle = upload;
    request.expected_bytes = expected_bytes;
    (void)memcpy(request.expected_sha256, expected_sha256,
        sizeof(request.expected_sha256));
    long status = opengat_syscall1(OPENGAT_SYS_PACKAGE_UPLOAD_SEAL,
        (uint64_t)(uintptr_t)&request);

    report->actual_bytes = request.actual_bytes;
    (void)memcpy(report->actual_sha256, request.actual_sha256,
        sizeof(report->actual_sha256));
    report->result_flags = request.result_flags;
    return status;
}

long opengat_package_upload_close(opengat_handle_t upload)
{
    return opengat_handle_close(upload);
}
