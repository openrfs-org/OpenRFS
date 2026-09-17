/* SPDX-License-Identifier: GPL-3.0-only */
#include <openrfs/package_upload.h>

#include <openrfs/runtime.h>
#include <string.h>

long openrfs_package_upload_open(void)
{
    return openrfs_syscall0(OPENRFS_SYS_PACKAGE_UPLOAD_OPEN);
}

long openrfs_package_upload_write(
    openrfs_handle_t upload,
    const void *bytes,
    size_t byte_count
)
{
    const struct openrfs_package_upload_write_request request = {
        sizeof(request), OPENRFS_ABI_VERSION, upload,
        (uint64_t)(uintptr_t)bytes, (uint32_t)byte_count, 0U
    };

    if ((bytes == NULL && byte_count != 0U) ||
        byte_count > OPENRFS_PACKAGE_UPLOAD_WRITE_MAX) {
        return -OPENRFS_EINVAL;
    }
    return openrfs_syscall1(OPENRFS_SYS_PACKAGE_UPLOAD_WRITE,
        (uint64_t)(uintptr_t)&request);
}

long openrfs_package_upload_seal(
    openrfs_handle_t upload,
    uint64_t expected_bytes,
    const uint8_t expected_sha256[OPENRFS_PACKAGE_UPLOAD_SHA256_BYTES],
    struct openrfs_package_upload_report *report
)
{
    struct openrfs_package_upload_seal_request request;

    if (expected_sha256 == NULL || report == NULL || expected_bytes == 0U ||
        expected_bytes > OPENRFS_PACKAGE_UPLOAD_MAX_BYTES) {
        return -OPENRFS_EINVAL;
    }
    (void)memset(&request, 0, sizeof(request));
    request.size = sizeof(request);
    request.version = OPENRFS_ABI_VERSION;
    request.handle = upload;
    request.expected_bytes = expected_bytes;
    (void)memcpy(request.expected_sha256, expected_sha256,
        sizeof(request.expected_sha256));
    long status = openrfs_syscall1(OPENRFS_SYS_PACKAGE_UPLOAD_SEAL,
        (uint64_t)(uintptr_t)&request);

    report->actual_bytes = request.actual_bytes;
    (void)memcpy(report->actual_sha256, request.actual_sha256,
        sizeof(report->actual_sha256));
    report->result_flags = request.result_flags;
    return status;
}

long openrfs_package_upload_close(openrfs_handle_t upload)
{
    return openrfs_handle_close(upload);
}
