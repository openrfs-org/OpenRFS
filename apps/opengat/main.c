/* SPDX-License-Identifier: GPL-3.0-only */
/* Bounded signed-repository install/update client for the native ABI. */

#include <opengat/package_control.h>
#include <opengat/package_fetch.h>
#include <opengat/package_upload.h>
#include <opengat/runtime.h>

#include <stdio.h>
#include <string.h>

#include "../native-https/trust_anchor.h"

#define REPOSITORY_HOST "repo.opengat.test"
#define REPOSITORY_PATH "/repository.sri"
#define HTTPS_PORT 443U
#define HTTPS_DEADLINE_NS UINT64_C(180000000000)
#define REPOSITORY_MAX_BYTES (512U * 1024U)
#define COPY_BYTES 4096U

static uint64_t deadline(void)
{
    const uint64_t now = opengat_monotonic_ns();

    return now > UINT64_MAX - HTTPS_DEADLINE_NS ? UINT64_MAX :
        now + HTTPS_DEADLINE_NS;
}

static bool close_handle(opengat_handle_t handle)
{
    return handle == OPENGAT_HANDLE_INVALID || opengat_handle_close(handle) == 0;
}

static bool discard_repository(void)
{
    long unlinked = opengat_path_unlink(OPENGAT_VOLUME_DATA, "REPO.SRI");
    long synced = opengat_volume_sync(OPENGAT_VOLUME_DATA);

    return unlinked == 0 && synced == 0;
}

static long repository_upload(struct opengat_package_fetch_report *fetch)
{
    const struct opengat_package_fetch_request request = {
        REPOSITORY_HOST, HTTPS_PORT, 0U, REPOSITORY_PATH,
        opengat_https_test_anchors,
        sizeof(opengat_https_test_anchors) /
            sizeof(opengat_https_test_anchors[0]),
        deadline(), REPOSITORY_MAX_BYTES, 0U, NULL,
        "REPO.NEW", "REPO.SRI"
    };
    uint8_t buffer[COPY_BYTES];
    opengat_handle_t file = OPENGAT_HANDLE_INVALID;
    opengat_handle_t upload = OPENGAT_HANDLE_INVALID;
    size_t total = 0U;

    enum opengat_package_fetch_status status = opengat_package_fetch_stage(
        &request, fetch);
    if (status != OPENGAT_PACKAGE_FETCH_OK || fetch->bytes_received == 0U ||
        fetch->bytes_received > REPOSITORY_MAX_BYTES || !fetch->durable) {
        return -OPENGAT_EIO;
    }
    long opened = opengat_file_open(OPENGAT_VOLUME_DATA, "REPO.SRI",
        OPENGAT_OPEN_READ);
    if (opened < 0) {
        return opened;
    }
    file = (opengat_handle_t)opened;
    opened = opengat_package_upload_open();
    if (opened < 0) {
        (void)close_handle(file);
        return opened;
    }
    upload = (opengat_handle_t)opened;
    while (total < fetch->bytes_received) {
        size_t wanted = fetch->bytes_received - total;
        if (wanted > sizeof(buffer)) {
            wanted = sizeof(buffer);
        }
        long read_bytes = opengat_file_read(file, buffer, wanted);
        if (read_bytes <= 0 || (size_t)read_bytes > wanted) {
            (void)close_handle(file);
            (void)close_handle(upload);
            return read_bytes < 0 ? read_bytes : -OPENGAT_EIO;
        }
        long written = opengat_package_upload_write(upload, buffer,
            (size_t)read_bytes);
        if (written != read_bytes) {
            (void)close_handle(file);
            (void)close_handle(upload);
            return written < 0 ? written : -OPENGAT_EIO;
        }
        total += (size_t)read_bytes;
    }
    if (!close_handle(file)) {
        (void)close_handle(upload);
        return -OPENGAT_EIO;
    }
    struct opengat_package_upload_report sealed;
    long result = opengat_package_upload_seal(upload, fetch->bytes_received,
        fetch->sha256, &sealed);
    if (result < 0 || sealed.actual_bytes != fetch->bytes_received ||
        sealed.result_flags != (OPENGAT_PACKAGE_UPLOAD_SEALED |
            OPENGAT_PACKAGE_UPLOAD_DURABLE)) {
        (void)close_handle(upload);
        return result < 0 ? result : -OPENGAT_EIO;
    }
    return (long)upload;
}

static int install_or_repair(const char *identifier, bool repair)
{
    struct opengat_package_fetch_report fetch;
    struct opengat_package_control_report control_report;
    struct opengat_package_control_item item;
    opengat_handle_t repository = OPENGAT_HANDLE_INVALID;
    opengat_handle_t control = OPENGAT_HANDLE_INVALID;

    long result = repository_upload(&fetch);
    if (result < 0) {
        printf("opengat: repository download failed: %ld\n", result);
        return 20;
    }
    repository = (opengat_handle_t)result;
    result = repair ? opengat_package_control_open_repair(repository,
        &control_report) : opengat_package_control_open_install(repository,
            identifier, strlen(identifier), &control_report);
    bool repository_closed = close_handle(repository);
    bool repository_discarded = discard_repository();
    if (!repository_closed || !repository_discarded) {
        if (result >= 0) {
            (void)close_handle((opengat_handle_t)result);
        }
        return 22;
    }
    if (result == -(long)OPENGAT_EEXIST) {
        puts("OPENGAT PACKAGE PHASE already-installed PASS");
        return 0;
    }
    if (result < 0) {
        puts("OPENGAT PACKAGE PHASE signed-plan-refused PASS");
        printf("opengat: signed repository or plan refused: %ld\n", result);
        return 21;
    }
    control = (opengat_handle_t)result;
    puts(repair ? "OPENGAT PACKAGE PHASE repair-plan PASS" :
        "OPENGAT PACKAGE PHASE signed-plan PASS");

    for (uint32_t index = 0U; index < control_report.plan_count; ++index) {
        char path[OPENGAT_PACKAGE_CONTROL_PATH_BYTES + 2U];
        if (opengat_package_control_item(control, index, &item) < 0 ||
            item.path_bytes == 0U ||
            item.path_bytes >= OPENGAT_PACKAGE_CONTROL_PATH_BYTES) {
            (void)close_handle(control);
            return 23;
        }
        path[0] = '/';
        (void)memcpy(path + 1U, item.download_path, item.path_bytes);
        path[item.path_bytes + 1U] = '\0';
        const struct opengat_package_fetch_upload_request request = {
            REPOSITORY_HOST, HTTPS_PORT, 0U, path,
            opengat_https_test_anchors,
            sizeof(opengat_https_test_anchors) /
                sizeof(opengat_https_test_anchors[0]),
            deadline(), (size_t)item.package_bytes, item.package_sha256
        };
        enum opengat_package_fetch_status status = opengat_package_fetch_upload(
            &request, &fetch);
        if (status != OPENGAT_PACKAGE_FETCH_OK ||
            fetch.upload == OPENGAT_HANDLE_INVALID || !fetch.durable) {
            printf("opengat: payload %u download failed: %s https=%s tls=%d "
                "transport=%ld storage=%ld cleanup=%ld bytes=%zu\n", index,
                opengat_package_fetch_status_string(status),
                opengat_https_status_string(fetch.https_status),
                fetch.bearssl_error, fetch.transport_error,
                fetch.storage_error, fetch.cleanup_error,
                fetch.bytes_received);
            if (fetch.upload != OPENGAT_HANDLE_INVALID) {
                (void)close_handle(fetch.upload);
            }
            (void)close_handle(control);
            return 24;
        }
        result = opengat_package_control_attach(control, index, fetch.upload,
            &control_report);
        if (!close_handle(fetch.upload) || result < 0) {
            printf("opengat: payload %u refused: %ld\n", index, result);
            (void)close_handle(control);
            return 25;
        }
    }
    puts("OPENGAT PACKAGE PHASE payloads-authenticated PASS");
    result = opengat_package_control_commit(control, &control_report);
    if (result < 0 &&
        (control_report.result_flags & OPENGAT_PACKAGE_CONTROL_PREPARED) != 0U) {
        result = opengat_package_control_commit(control, &control_report);
    }
    if (result < 0 ||
        (control_report.result_flags & OPENGAT_PACKAGE_CONTROL_COMMITTED) == 0U ||
        control_report.generation == 0U || !close_handle(control)) {
        printf("opengat: transaction commit failed: %ld flags=%u\n", result,
            control_report.result_flags);
        return 26;
    }
    printf(repair ?
        "OPENGAT PACKAGE PHASE repaired generation=%llu PASS\n" :
        "OPENGAT PACKAGE PHASE committed generation=%llu PASS\n",
        (unsigned long long)control_report.generation);
    return 0;
}

static int remove_package(const char *identifier)
{
    struct opengat_package_control_report report;
    long result = opengat_package_control_open_remove(identifier,
        strlen(identifier), &report);

    if (result < 0) {
        printf("opengat: removal plan refused: %ld\n", result);
        return 30;
    }
    opengat_handle_t control = (opengat_handle_t)result;
    puts("OPENGAT PACKAGE PHASE remove-plan PASS");
    result = opengat_package_control_commit(control, &report);
    if (result < 0 &&
        (report.result_flags & OPENGAT_PACKAGE_CONTROL_PREPARED) != 0U) {
        result = opengat_package_control_commit(control, &report);
    }
    if (result < 0 ||
        (report.result_flags & OPENGAT_PACKAGE_CONTROL_COMMITTED) == 0U ||
        report.generation == 0U || !close_handle(control)) {
        printf("opengat: removal commit failed: %ld flags=%u\n", result,
            report.result_flags);
        return 31;
    }
    printf("OPENGAT PACKAGE PHASE removed generation=%llu PASS\n",
        (unsigned long long)report.generation);
    return 0;
}

int main(int argc, char **argv)
{
    const bool repair = argc == 2 && strcmp(argv[1], "repair") == 0;

    if (!repair && (argc != 3 || (strcmp(argv[1], "install") != 0 &&
            strcmp(argv[1], "remove") != 0))) {
        puts("usage: opengat install|remove IDENTIFIER | opengat repair");
        return 2;
    }
    puts("OPENGAT PACKAGE PHASE start");
    int result = repair ? install_or_repair(NULL, true) :
        (strcmp(argv[1], "remove") == 0 ? remove_package(argv[2]) :
            install_or_repair(argv[2], false));
    if (result == 0) {
        puts(repair ?
            "OPENGAT PACKAGE REPAIR PASS trust payload transaction cleanup" :
            (strcmp(argv[1], "remove") == 0 ?
            "OPENGAT PACKAGE REMOVE PASS trust plan transaction cleanup" :
            "OPENGAT PACKAGE PASS https trust plan payload transaction cleanup"));
    }
    return result;
}
