/* SPDX-License-Identifier: GPL-3.0-only */
#include <openrfs/package_control.h>

#include <openrfs/runtime.h>
#include <string.h>

static void report_clear(struct openrfs_package_control_report *report)
{
    if (report != NULL) {
        (void)memset(report, 0, sizeof(*report));
    }
}

static void report_open(struct openrfs_package_control_report *report,
    const struct openrfs_package_control_open_request *request)
{
    if (report != NULL) {
        report->repository_version = request->repository_version;
        report->generation = request->generation;
        report->plan_count = request->plan_count;
        report->result_flags = request->result_flags;
    }
}

long openrfs_package_control_open_install(
    openrfs_handle_t repository_upload,
    const char *identifier,
    size_t identifier_bytes,
    struct openrfs_package_control_report *report
)
{
    struct openrfs_package_control_open_request request;

    report_clear(report);
    if (repository_upload == OPENRFS_HANDLE_INVALID || identifier == NULL ||
        identifier_bytes == 0U ||
        identifier_bytes >= OPENRFS_PACKAGE_CONTROL_TEXT_BYTES ||
        report == NULL) {
        return -OPENRFS_EINVAL;
    }
    (void)memset(&request, 0, sizeof(request));
    request.size = sizeof(request);
    request.version = OPENRFS_ABI_VERSION;
    request.repository_upload = repository_upload;
    request.identifier = (uint64_t)(uintptr_t)identifier;
    request.identifier_bytes = (uint32_t)identifier_bytes;
    long status = openrfs_syscall1(OPENRFS_SYS_PACKAGE_CONTROL_OPEN_INSTALL,
        (uint64_t)(uintptr_t)&request);

    report_open(report, &request);
    return status;
}

long openrfs_package_control_open_remove(
    const char *identifier,
    size_t identifier_bytes,
    struct openrfs_package_control_report *report
)
{
    struct openrfs_package_control_open_request request;

    report_clear(report);
    if (identifier == NULL || identifier_bytes == 0U ||
        identifier_bytes >= OPENRFS_PACKAGE_CONTROL_TEXT_BYTES ||
        report == NULL) {
        return -OPENRFS_EINVAL;
    }
    (void)memset(&request, 0, sizeof(request));
    request.size = sizeof(request);
    request.version = OPENRFS_ABI_VERSION;
    request.identifier = (uint64_t)(uintptr_t)identifier;
    request.identifier_bytes = (uint32_t)identifier_bytes;
    request.flags = OPENRFS_PACKAGE_CONTROL_OPEN_REMOVE;
    long status = openrfs_syscall1(OPENRFS_SYS_PACKAGE_CONTROL_OPEN_INSTALL,
        (uint64_t)(uintptr_t)&request);

    report_open(report, &request);
    return status;
}

long openrfs_package_control_open_repair(
    openrfs_handle_t repository_upload,
    struct openrfs_package_control_report *report
)
{
    struct openrfs_package_control_open_request request;

    report_clear(report);
    if (repository_upload == OPENRFS_HANDLE_INVALID || report == NULL) {
        return -OPENRFS_EINVAL;
    }
    (void)memset(&request, 0, sizeof(request));
    request.size = sizeof(request);
    request.version = OPENRFS_ABI_VERSION;
    request.repository_upload = repository_upload;
    request.flags = OPENRFS_PACKAGE_CONTROL_OPEN_REPAIR;
    long status = openrfs_syscall1(OPENRFS_SYS_PACKAGE_CONTROL_OPEN_INSTALL,
        (uint64_t)(uintptr_t)&request);

    report_open(report, &request);
    return status;
}

long openrfs_package_control_item(
    openrfs_handle_t control,
    uint32_t index,
    struct openrfs_package_control_item *item
)
{
    struct openrfs_package_control_item_request request;

    if (control == OPENRFS_HANDLE_INVALID || item == NULL ||
        index >= OPENRFS_PACKAGE_CONTROL_PLAN_MAX) {
        return -OPENRFS_EINVAL;
    }
    (void)memset(item, 0, sizeof(*item));
    (void)memset(&request, 0, sizeof(request));
    request.size = sizeof(request);
    request.version = OPENRFS_ABI_VERSION;
    request.control = control;
    request.index = index;
    long status = openrfs_syscall1(OPENRFS_SYS_PACKAGE_CONTROL_ITEM,
        (uint64_t)(uintptr_t)&request);

    if (status != 0) {
        return status;
    }
    item->index = index;
    item->identifier_bytes = request.identifier_bytes;
    item->version_bytes = request.version_bytes;
    item->path_bytes = request.path_bytes;
    item->package_bytes = request.package_bytes;
    (void)memcpy(item->package_sha256, request.package_sha256,
        sizeof(item->package_sha256));
    (void)memcpy(item->identifier, request.identifier,
        sizeof(item->identifier));
    (void)memcpy(item->version, request.package_version,
        sizeof(item->version));
    (void)memcpy(item->download_path, request.download_path,
        sizeof(item->download_path));
    return 0;
}

long openrfs_package_control_attach(
    openrfs_handle_t control,
    uint32_t index,
    openrfs_handle_t package_upload,
    struct openrfs_package_control_report *report
)
{
    struct openrfs_package_control_attach_request request;

    report_clear(report);
    if (control == OPENRFS_HANDLE_INVALID ||
        package_upload == OPENRFS_HANDLE_INVALID ||
        index >= OPENRFS_PACKAGE_CONTROL_PLAN_MAX || report == NULL) {
        return -OPENRFS_EINVAL;
    }
    (void)memset(&request, 0, sizeof(request));
    request.size = sizeof(request);
    request.version = OPENRFS_ABI_VERSION;
    request.control = control;
    request.index = index;
    request.package_upload = package_upload;
    long status = openrfs_syscall1(OPENRFS_SYS_PACKAGE_CONTROL_ATTACH,
        (uint64_t)(uintptr_t)&request);

    report->attached_count = request.attached_count;
    report->result_flags = request.result_flags;
    return status;
}

long openrfs_package_control_commit(
    openrfs_handle_t control,
    struct openrfs_package_control_report *report
)
{
    struct openrfs_package_control_commit_request request;

    report_clear(report);
    if (control == OPENRFS_HANDLE_INVALID || report == NULL) {
        return -OPENRFS_EINVAL;
    }
    (void)memset(&request, 0, sizeof(request));
    request.size = sizeof(request);
    request.version = OPENRFS_ABI_VERSION;
    request.control = control;
    long status = openrfs_syscall1(OPENRFS_SYS_PACKAGE_CONTROL_COMMIT,
        (uint64_t)(uintptr_t)&request);

    report->generation = request.generation;
    report->plan_count = request.plan_count;
    report->attached_count = request.attached_count;
    report->result_flags = request.result_flags;
    return status;
}

long openrfs_package_control_close(openrfs_handle_t control)
{
    return openrfs_handle_close(control);
}
