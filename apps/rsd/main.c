/* SPDX-License-Identifier: GPL-3.0-only */
/* Bounded signed-repository install/update client for the native ABI. */

#include <rsd/package_control.h>
#include <rsd/package_fetch.h>
#include <rsd/package_upload.h>
#include <rsd/runtime.h>

#include <stdio.h>
#include <string.h>

#include "../native-https/trust_anchor.h"

#define REPOSITORY_HOST "repo.rsd.test"
#define REPOSITORY_PATH "/repository.sri"
#define HTTPS_PORT 443U
#define HTTPS_DEADLINE_NS UINT64_C(300000000000)
#define REPOSITORY_MAX_BYTES (512U * 1024U)
#define COPY_BYTES 4096U

static uint64_t deadline(void)
{
    const uint64_t now = rsd_monotonic_ns();

    return now > UINT64_MAX - HTTPS_DEADLINE_NS ? UINT64_MAX :
        now + HTTPS_DEADLINE_NS;
}

static bool close_handle(rsd_handle_t handle)
{
    return handle == RSD_HANDLE_INVALID || rsd_handle_close(handle) == 0;
}

static bool discard_repository(void)
{
    long unlinked = rsd_path_unlink(RSD_VOLUME_DATA, "REPO.SRI");
    long synced = rsd_volume_sync(RSD_VOLUME_DATA);

    return unlinked == 0 && synced == 0;
}

static long repository_upload(struct rsd_package_fetch_report *fetch)
{
    const struct rsd_package_fetch_request request = {
        REPOSITORY_HOST, HTTPS_PORT, 0U, REPOSITORY_PATH,
        rsd_https_test_anchors,
        sizeof(rsd_https_test_anchors) /
            sizeof(rsd_https_test_anchors[0]),
        deadline(), REPOSITORY_MAX_BYTES, 0U, NULL,
        "REPO.NEW", "REPO.SRI"
    };
    uint8_t buffer[COPY_BYTES];
    rsd_handle_t file = RSD_HANDLE_INVALID;
    rsd_handle_t upload = RSD_HANDLE_INVALID;
    size_t total = 0U;

    enum rsd_package_fetch_status status = rsd_package_fetch_stage(
        &request, fetch);
    if (status != RSD_PACKAGE_FETCH_OK || fetch->bytes_received == 0U ||
        fetch->bytes_received > REPOSITORY_MAX_BYTES || !fetch->durable) {
        return -RSD_EIO;
    }
    long opened = rsd_file_open(RSD_VOLUME_DATA, "REPO.SRI",
        RSD_OPEN_READ);
    if (opened < 0) {
        return opened;
    }
    file = (rsd_handle_t)opened;
    opened = rsd_package_upload_open();
    if (opened < 0) {
        (void)close_handle(file);
        return opened;
    }
    upload = (rsd_handle_t)opened;
    while (total < fetch->bytes_received) {
        size_t wanted = fetch->bytes_received - total;
        if (wanted > sizeof(buffer)) {
            wanted = sizeof(buffer);
        }
        long read_bytes = rsd_file_read(file, buffer, wanted);
        if (read_bytes <= 0 || (size_t)read_bytes > wanted) {
            (void)close_handle(file);
            (void)close_handle(upload);
            return read_bytes < 0 ? read_bytes : -RSD_EIO;
        }
        long written = rsd_package_upload_write(upload, buffer,
            (size_t)read_bytes);
        if (written != read_bytes) {
            (void)close_handle(file);
            (void)close_handle(upload);
            return written < 0 ? written : -RSD_EIO;
        }
        total += (size_t)read_bytes;
    }
    if (!close_handle(file)) {
        (void)close_handle(upload);
        return -RSD_EIO;
    }
    struct rsd_package_upload_report sealed;
    long result = rsd_package_upload_seal(upload, fetch->bytes_received,
        fetch->sha256, &sealed);
    if (result < 0 || sealed.actual_bytes != fetch->bytes_received ||
        sealed.result_flags != (RSD_PACKAGE_UPLOAD_SEALED |
            RSD_PACKAGE_UPLOAD_DURABLE)) {
        (void)close_handle(upload);
        return result < 0 ? result : -RSD_EIO;
    }
    return (long)upload;
}

static int install_or_repair(const char *identifier, bool repair)
{
    struct rsd_package_fetch_report fetch;
    struct rsd_package_control_report control_report;
    struct rsd_package_control_item item;
    rsd_handle_t repository = RSD_HANDLE_INVALID;
    rsd_handle_t control = RSD_HANDLE_INVALID;

    long result = repository_upload(&fetch);
    if (result < 0) {
        printf("rsd: repository download failed: %ld\n", result);
        return 20;
    }
    repository = (rsd_handle_t)result;
    result = repair ? rsd_package_control_open_repair(repository,
        &control_report) : rsd_package_control_open_install(repository,
            identifier, strlen(identifier), &control_report);
    bool repository_closed = close_handle(repository);
    bool repository_discarded = discard_repository();
    if (!repository_closed || !repository_discarded) {
        if (result >= 0) {
            (void)close_handle((rsd_handle_t)result);
        }
        return 22;
    }
    if (result == -(long)RSD_EEXIST) {
        puts("RSD PACKAGE PHASE already-installed PASS");
        return 0;
    }
    if (result < 0) {
        puts("RSD PACKAGE PHASE signed-plan-refused PASS");
        printf("rsd: signed repository or plan refused: %ld\n", result);
        return 21;
    }
    control = (rsd_handle_t)result;
    puts(repair ? "RSD PACKAGE PHASE repair-plan PASS" :
        "RSD PACKAGE PHASE signed-plan PASS");

    for (uint32_t index = 0U; index < control_report.plan_count; ++index) {
        char path[RSD_PACKAGE_CONTROL_PATH_BYTES + 2U];
        if (rsd_package_control_item(control, index, &item) < 0 ||
            item.path_bytes == 0U ||
            item.path_bytes >= RSD_PACKAGE_CONTROL_PATH_BYTES) {
            (void)close_handle(control);
            return 23;
        }
        path[0] = '/';
        (void)memcpy(path + 1U, item.download_path, item.path_bytes);
        path[item.path_bytes + 1U] = '\0';
        const struct rsd_package_fetch_upload_request request = {
            REPOSITORY_HOST, HTTPS_PORT, 0U, path,
            rsd_https_test_anchors,
            sizeof(rsd_https_test_anchors) /
                sizeof(rsd_https_test_anchors[0]),
            deadline(), (size_t)item.package_bytes, item.package_sha256
        };
        enum rsd_package_fetch_status status = rsd_package_fetch_upload(
            &request, &fetch);
        if (status != RSD_PACKAGE_FETCH_OK ||
            fetch.upload == RSD_HANDLE_INVALID || !fetch.durable) {
            printf("rsd: payload %u download failed: %s https=%s tls=%d "
                "transport=%ld storage=%ld cleanup=%ld bytes=%zu\n", index,
                rsd_package_fetch_status_string(status),
                rsd_https_status_string(fetch.https_status),
                fetch.bearssl_error, fetch.transport_error,
                fetch.storage_error, fetch.cleanup_error,
                fetch.bytes_received);
            if (fetch.upload != RSD_HANDLE_INVALID) {
                (void)close_handle(fetch.upload);
            }
            (void)close_handle(control);
            return 24;
        }
        result = rsd_package_control_attach(control, index, fetch.upload,
            &control_report);
        if (!close_handle(fetch.upload) || result < 0) {
            printf("rsd: payload %u refused: %ld\n", index, result);
            (void)close_handle(control);
            return 25;
        }
    }
    puts("RSD PACKAGE PHASE payloads-authenticated PASS");
    result = rsd_package_control_commit(control, &control_report);
    if (result < 0 &&
        (control_report.result_flags & RSD_PACKAGE_CONTROL_PREPARED) != 0U) {
        result = rsd_package_control_commit(control, &control_report);
    }
    if (result < 0 ||
        (control_report.result_flags & RSD_PACKAGE_CONTROL_COMMITTED) == 0U ||
        control_report.generation == 0U || !close_handle(control)) {
        printf("rsd: transaction commit failed: %ld flags=%u\n", result,
            control_report.result_flags);
        return 26;
    }
    printf(repair ?
        "RSD PACKAGE PHASE repaired generation=%llu PASS\n" :
        "RSD PACKAGE PHASE committed generation=%llu PASS\n",
        (unsigned long long)control_report.generation);
    return 0;
}

static int remove_package(const char *identifier)
{
    struct rsd_package_control_report report;
    long result = rsd_package_control_open_remove(identifier,
        strlen(identifier), &report);

    if (result < 0) {
        printf("rsd: removal plan refused: %ld\n", result);
        return 30;
    }
    rsd_handle_t control = (rsd_handle_t)result;
    puts("RSD PACKAGE PHASE remove-plan PASS");
    result = rsd_package_control_commit(control, &report);
    if (result < 0 &&
        (report.result_flags & RSD_PACKAGE_CONTROL_PREPARED) != 0U) {
        result = rsd_package_control_commit(control, &report);
    }
    if (result < 0 ||
        (report.result_flags & RSD_PACKAGE_CONTROL_COMMITTED) == 0U ||
        report.generation == 0U || !close_handle(control)) {
        printf("rsd: removal commit failed: %ld flags=%u\n", result,
            report.result_flags);
        return 31;
    }
    printf("RSD PACKAGE PHASE removed generation=%llu PASS\n",
        (unsigned long long)report.generation);
    return 0;
}

int main(int argc, char **argv)
{
    const bool repair = argc == 2 && strcmp(argv[1], "repair") == 0;

    if (!repair && (argc != 3 || (strcmp(argv[1], "install") != 0 &&
            strcmp(argv[1], "remove") != 0))) {
        puts("usage: rsd install|remove IDENTIFIER | rsd repair");
        return 2;
    }
    puts("RSD PACKAGE PHASE start");
    int result = repair ? install_or_repair(NULL, true) :
        (strcmp(argv[1], "remove") == 0 ? remove_package(argv[2]) :
            install_or_repair(argv[2], false));
    if (result == 0) {
        puts(repair ?
            "RSD PACKAGE REPAIR PASS trust payload transaction cleanup" :
            (strcmp(argv[1], "remove") == 0 ?
            "RSD PACKAGE REMOVE PASS trust plan transaction cleanup" :
            "RSD PACKAGE PASS https trust plan payload transaction cleanup"));
    }
    return result;
}
