/* SPDX-License-Identifier: GPL-3.0-only */
#include <trait/package_control.h>

#include <trait/runtime.h>
#include <string.h>

static void report_clear(struct trait_package_control_report *report)
{
    if (report != NULL) {
        (void)memset(report, 0, sizeof(*report));
    }
}

static void report_open(struct trait_package_control_report *report,
    const struct trait_package_control_open_request *request)
{
    if (report != NULL) {
        report->repository_version = request->repository_version;
        report->generation = request->generation;
        report->plan_count = request->plan_count;
        report->result_flags = request->result_flags;
    }
}

long trait_package_control_open_install(
    trait_handle_t repository_upload,
    const char *identifier,
    size_t identifier_bytes,
    struct trait_package_control_report *report
)
{
    struct trait_package_control_open_request request;

    report_clear(report);
    if (repository_upload == TRAIT_HANDLE_INVALID || identifier == NULL ||
        identifier_bytes == 0U ||
        identifier_bytes >= TRAIT_PACKAGE_CONTROL_TEXT_BYTES ||
        report == NULL) {
        return -TRAIT_EINVAL;
    }
    (void)memset(&request, 0, sizeof(request));
    request.size = sizeof(request);
    request.version = TRAIT_ABI_VERSION;
    request.repository_upload = repository_upload;
    request.identifier = (uint64_t)(uintptr_t)identifier;
    request.identifier_bytes = (uint32_t)identifier_bytes;
    long status = trait_syscall1(TRAIT_SYS_PACKAGE_CONTROL_OPEN_INSTALL,
        (uint64_t)(uintptr_t)&request);

    report_open(report, &request);
    return status;
}

long trait_package_control_open_remove(
    const char *identifier,
    size_t identifier_bytes,
    struct trait_package_control_report *report
)
{
    struct trait_package_control_open_request request;

    report_clear(report);
    if (identifier == NULL || identifier_bytes == 0U ||
        identifier_bytes >= TRAIT_PACKAGE_CONTROL_TEXT_BYTES ||
        report == NULL) {
        return -TRAIT_EINVAL;
    }
    (void)memset(&request, 0, sizeof(request));
    request.size = sizeof(request);
    request.version = TRAIT_ABI_VERSION;
    request.identifier = (uint64_t)(uintptr_t)identifier;
    request.identifier_bytes = (uint32_t)identifier_bytes;
    request.flags = TRAIT_PACKAGE_CONTROL_OPEN_REMOVE;
    long status = trait_syscall1(TRAIT_SYS_PACKAGE_CONTROL_OPEN_INSTALL,
        (uint64_t)(uintptr_t)&request);

    report_open(report, &request);
    return status;
}

long trait_package_control_open_repair(
    trait_handle_t repository_upload,
    struct trait_package_control_report *report
)
{
    struct trait_package_control_open_request request;

    report_clear(report);
    if (repository_upload == TRAIT_HANDLE_INVALID || report == NULL) {
        return -TRAIT_EINVAL;
    }
    (void)memset(&request, 0, sizeof(request));
    request.size = sizeof(request);
    request.version = TRAIT_ABI_VERSION;
    request.repository_upload = repository_upload;
    request.flags = TRAIT_PACKAGE_CONTROL_OPEN_REPAIR;
    long status = trait_syscall1(TRAIT_SYS_PACKAGE_CONTROL_OPEN_INSTALL,
        (uint64_t)(uintptr_t)&request);

    report_open(report, &request);
    return status;
}

long trait_package_control_item(
    trait_handle_t control,
    uint32_t index,
    struct trait_package_control_item *item
)
{
    struct trait_package_control_item_request request;

    if (control == TRAIT_HANDLE_INVALID || item == NULL ||
        index >= TRAIT_PACKAGE_CONTROL_PLAN_MAX) {
        return -TRAIT_EINVAL;
    }
    (void)memset(item, 0, sizeof(*item));
    (void)memset(&request, 0, sizeof(request));
    request.size = sizeof(request);
    request.version = TRAIT_ABI_VERSION;
    request.control = control;
    request.index = index;
    long status = trait_syscall1(TRAIT_SYS_PACKAGE_CONTROL_ITEM,
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

long trait_package_control_attach(
    trait_handle_t control,
    uint32_t index,
    trait_handle_t package_upload,
    struct trait_package_control_report *report
)
{
    struct trait_package_control_attach_request request;

    report_clear(report);
    if (control == TRAIT_HANDLE_INVALID ||
        package_upload == TRAIT_HANDLE_INVALID ||
        index >= TRAIT_PACKAGE_CONTROL_PLAN_MAX || report == NULL) {
        return -TRAIT_EINVAL;
    }
    (void)memset(&request, 0, sizeof(request));
    request.size = sizeof(request);
    request.version = TRAIT_ABI_VERSION;
    request.control = control;
    request.index = index;
    request.package_upload = package_upload;
    long status = trait_syscall1(TRAIT_SYS_PACKAGE_CONTROL_ATTACH,
        (uint64_t)(uintptr_t)&request);

    report->attached_count = request.attached_count;
    report->result_flags = request.result_flags;
    return status;
}

long trait_package_control_commit(
    trait_handle_t control,
    struct trait_package_control_report *report
)
{
    struct trait_package_control_commit_request request;

    report_clear(report);
    if (control == TRAIT_HANDLE_INVALID || report == NULL) {
        return -TRAIT_EINVAL;
    }
    (void)memset(&request, 0, sizeof(request));
    request.size = sizeof(request);
    request.version = TRAIT_ABI_VERSION;
    request.control = control;
    long status = trait_syscall1(TRAIT_SYS_PACKAGE_CONTROL_COMMIT,
        (uint64_t)(uintptr_t)&request);

    report->generation = request.generation;
    report->plan_count = request.plan_count;
    report->attached_count = request.attached_count;
    report->result_flags = request.result_flags;
    return status;
}

long trait_package_control_close(trait_handle_t control)
{
    return trait_handle_close(control);
}
