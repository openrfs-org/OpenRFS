/* SPDX-License-Identifier: GPL-3.0-only */
#include <rsd/package_upload.h>

#include <rsd/runtime.h>
#include <string.h>

long rsd_package_upload_open(void)
{
    return rsd_syscall0(RSD_SYS_PACKAGE_UPLOAD_OPEN);
}

long rsd_package_upload_write(
    rsd_handle_t upload,
    const void *bytes,
    size_t byte_count
)
{
    const struct rsd_package_upload_write_request request = {
        sizeof(request), RSD_ABI_VERSION, upload,
        (uint64_t)(uintptr_t)bytes, (uint32_t)byte_count, 0U
    };

    if ((bytes == NULL && byte_count != 0U) ||
        byte_count > RSD_PACKAGE_UPLOAD_WRITE_MAX) {
        return -RSD_EINVAL;
    }
    return rsd_syscall1(RSD_SYS_PACKAGE_UPLOAD_WRITE,
        (uint64_t)(uintptr_t)&request);
}

long rsd_package_upload_seal(
    rsd_handle_t upload,
    uint64_t expected_bytes,
    const uint8_t expected_sha256[RSD_PACKAGE_UPLOAD_SHA256_BYTES],
    struct rsd_package_upload_report *report
)
{
    struct rsd_package_upload_seal_request request;

    if (expected_sha256 == NULL || report == NULL || expected_bytes == 0U ||
        expected_bytes > RSD_PACKAGE_UPLOAD_MAX_BYTES) {
        return -RSD_EINVAL;
    }
    (void)memset(&request, 0, sizeof(request));
    request.size = sizeof(request);
    request.version = RSD_ABI_VERSION;
    request.handle = upload;
    request.expected_bytes = expected_bytes;
    (void)memcpy(request.expected_sha256, expected_sha256,
        sizeof(request.expected_sha256));
    long status = rsd_syscall1(RSD_SYS_PACKAGE_UPLOAD_SEAL,
        (uint64_t)(uintptr_t)&request);

    report->actual_bytes = request.actual_bytes;
    (void)memcpy(report->actual_sha256, request.actual_sha256,
        sizeof(report->actual_sha256));
    report->result_flags = request.result_flags;
    return status;
}

long rsd_package_upload_close(rsd_handle_t upload)
{
    return rsd_handle_close(upload);
}
