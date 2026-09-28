/* SPDX-License-Identifier: GPL-3.0-only */
#include <rsd/package_fetch.h>

#include <bearssl.h>
#include <limits.h>
#include <rsd/abi.h>
#include <rsd/runtime.h>
#include <string.h>

struct fetch_sink {
    rsd_handle_t handle;
    br_sha256_context sha256;
    long error;
};

struct upload_sink {
    rsd_handle_t handle;
    long error;
};

static bool equal_bytes(const uint8_t *left, const uint8_t *right, size_t count)
{
    uint8_t difference = 0U;
    for (size_t index = 0U; index < count; ++index) {
        difference |= left[index] ^ right[index];
    }
    return difference == 0U;
}

static bool path_valid(const char *path)
{
    size_t length;
    if (path == NULL) {
        return false;
    }
    length = strlen(path);
    return length != 0U && length <= RSD_PATH_MAX;
}

static long write_body(void *context, const void *bytes, size_t byte_count)
{
    struct fetch_sink *sink = context;
    const uint8_t *source = bytes;
    size_t written = 0U;
    if (sink == NULL || bytes == NULL || byte_count > LONG_MAX) {
        return -1;
    }
    while (written < byte_count) {
        long count = rsd_file_write(sink->handle, source + written,
            byte_count - written);
        if (count <= 0 || (size_t)count > byte_count - written) {
            sink->error = count < 0 ? count : -(long)RSD_EIO;
            return -1;
        }
        br_sha256_update(&sink->sha256, source + written, (size_t)count);
        written += (size_t)count;
    }
    return (long)written;
}

static long write_upload_body(void *context, const void *bytes,
    size_t byte_count)
{
    struct upload_sink *sink = context;
    const uint8_t *source = bytes;
    size_t written = 0U;

    if (sink == NULL || bytes == NULL || byte_count > LONG_MAX) {
        return -1;
    }
    while (written < byte_count) {
        size_t chunk = byte_count - written;

        if (chunk > RSD_PACKAGE_UPLOAD_WRITE_MAX) {
            chunk = RSD_PACKAGE_UPLOAD_WRITE_MAX;
        }
        long count = rsd_package_upload_write(sink->handle,
            source + written, chunk);

        if (count <= 0 || (size_t)count > chunk) {
            sink->error = count < 0 ? count : -(long)RSD_EIO;
            return -1;
        }
        written += (size_t)count;
    }
    return (long)written;
}

static void cleanup_temporary(
    const struct rsd_package_fetch_request *request,
    struct rsd_package_fetch_report *report
)
{
    long status = rsd_path_unlink(RSD_VOLUME_DATA,
        request->temporary_path);
    if (status < 0 && status != -(long)RSD_ENOENT) {
        report->cleanup_error = status;
        return;
    }
    status = rsd_volume_sync(RSD_VOLUME_DATA);
    if (status < 0) {
        report->cleanup_error = status;
    }
}

static void cleanup_upload(
    rsd_handle_t upload,
    struct rsd_package_fetch_report *report
)
{
    long status = rsd_package_upload_close(upload);

    if (status < 0) {
        report->cleanup_error = status;
    } else {
        report->upload = RSD_HANDLE_INVALID;
    }
}

enum rsd_package_fetch_status rsd_package_fetch_stage(
    const struct rsd_package_fetch_request *request,
    struct rsd_package_fetch_report *report
)
{
    struct fetch_sink sink;
    struct rsd_https_stream_request stream;
    struct rsd_https_response response;
    long opened;
    long status;
    if (report == NULL) {
        return RSD_PACKAGE_FETCH_ARGUMENT;
    }
    (void)memset(report, 0, sizeof(*report));
    report->https_status = RSD_HTTPS_ARGUMENT;
    if (request == NULL || request->reserved != 0U ||
        !path_valid(request->temporary_path) ||
        !path_valid(request->staged_path) ||
        strcmp(request->temporary_path, request->staged_path) == 0 ||
        request->maximum_bytes == 0U ||
        request->maximum_bytes > RSD_PACKAGE_FETCH_MAX_BYTES ||
        request->expected_bytes > request->maximum_bytes ||
        ((request->expected_bytes == 0U) !=
            (request->expected_sha256 == NULL))) {
        return RSD_PACKAGE_FETCH_ARGUMENT;
    }
    opened = rsd_file_open(RSD_VOLUME_DATA, request->temporary_path,
        RSD_OPEN_WRITE | RSD_OPEN_CREATE | RSD_OPEN_TRUNCATE);
    if (opened < 0) {
        report->storage_error = opened;
        return RSD_PACKAGE_FETCH_OPEN;
    }
    sink.handle = (rsd_handle_t)opened;
    sink.error = 0;
    br_sha256_init(&sink.sha256);
    stream = (struct rsd_https_stream_request){
        request->hostname, request->port, request->reserved, request->path,
        request->trust_anchors, request->trust_anchor_count,
        request->deadline_ns, request->maximum_bytes, write_body, &sink
    };
    report->https_status = rsd_https_get_stream(&stream, &response);
    report->bearssl_error = response.bearssl_error;
    report->transport_error = response.transport_error;
    report->bytes_received = response.body_length;
    if (report->https_status == RSD_HTTPS_BODY_WRITE) {
        report->storage_error = sink.error;
    }
    status = rsd_handle_close(sink.handle);
    if (status < 0 && report->storage_error == 0) {
        report->storage_error = status;
    }
    if (report->https_status != RSD_HTTPS_OK) {
        cleanup_temporary(request, report);
        return report->https_status == RSD_HTTPS_BODY_WRITE ?
            RSD_PACKAGE_FETCH_WRITE : RSD_PACKAGE_FETCH_HTTPS;
    }
    if (status < 0) {
        cleanup_temporary(request, report);
        return RSD_PACKAGE_FETCH_CLOSE;
    }
    br_sha256_out(&sink.sha256, report->sha256);
    if (request->expected_bytes != 0U &&
        report->bytes_received != request->expected_bytes) {
        cleanup_temporary(request, report);
        return RSD_PACKAGE_FETCH_LENGTH;
    }
    if (request->expected_sha256 != NULL &&
        !equal_bytes(report->sha256, request->expected_sha256,
            sizeof(report->sha256))) {
        cleanup_temporary(request, report);
        return RSD_PACKAGE_FETCH_DIGEST;
    }
    status = rsd_volume_sync(RSD_VOLUME_DATA);
    if (status < 0) {
        report->storage_error = status;
        cleanup_temporary(request, report);
        return RSD_PACKAGE_FETCH_SYNC;
    }
    status = rsd_path_replace(RSD_VOLUME_DATA,
        request->temporary_path, request->staged_path);
    if (status < 0) {
        report->storage_error = status;
        cleanup_temporary(request, report);
        return RSD_PACKAGE_FETCH_PUBLISH;
    }
    report->published = true;
    status = rsd_volume_sync(RSD_VOLUME_DATA);
    if (status < 0) {
        report->storage_error = status;
        return RSD_PACKAGE_FETCH_SYNC;
    }
    report->durable = true;
    return RSD_PACKAGE_FETCH_OK;
}

enum rsd_package_fetch_status rsd_package_fetch_upload(
    const struct rsd_package_fetch_upload_request *request,
    struct rsd_package_fetch_report *report
)
{
    struct upload_sink sink;
    struct rsd_https_stream_request stream;
    struct rsd_https_response response;
    struct rsd_package_upload_report upload_report;
    long opened;
    long status;

    if (report == NULL) {
        return RSD_PACKAGE_FETCH_ARGUMENT;
    }
    (void)memset(report, 0, sizeof(*report));
    report->https_status = RSD_HTTPS_ARGUMENT;
    if (request == NULL || request->reserved != 0U ||
        request->expected_bytes == 0U ||
        request->expected_bytes > RSD_PACKAGE_FETCH_MAX_BYTES ||
        request->expected_sha256 == NULL) {
        return RSD_PACKAGE_FETCH_ARGUMENT;
    }
    opened = rsd_package_upload_open();
    if (opened < 0) {
        report->storage_error = opened;
        return RSD_PACKAGE_FETCH_UPLOAD_OPEN;
    }
    sink.handle = (rsd_handle_t)opened;
    sink.error = 0;
    report->upload = sink.handle;
    stream = (struct rsd_https_stream_request){
        request->hostname, request->port, request->reserved, request->path,
        request->trust_anchors, request->trust_anchor_count,
        request->deadline_ns, request->expected_bytes, write_upload_body, &sink
    };
    report->https_status = rsd_https_get_stream(&stream, &response);
    report->bearssl_error = response.bearssl_error;
    report->transport_error = response.transport_error;
    report->bytes_received = response.body_length;
    if (report->https_status == RSD_HTTPS_BODY_WRITE) {
        report->storage_error = sink.error;
    }
    if (report->https_status != RSD_HTTPS_OK) {
        cleanup_upload(sink.handle, report);
        return report->https_status == RSD_HTTPS_BODY_WRITE ?
            RSD_PACKAGE_FETCH_WRITE : RSD_PACKAGE_FETCH_HTTPS;
    }
    if (report->bytes_received != request->expected_bytes) {
        cleanup_upload(sink.handle, report);
        return RSD_PACKAGE_FETCH_LENGTH;
    }
    (void)memset(&upload_report, 0, sizeof(upload_report));
    status = rsd_package_upload_seal(sink.handle, request->expected_bytes,
        request->expected_sha256, &upload_report);
    report->bytes_received = (size_t)upload_report.actual_bytes;
    (void)memcpy(report->sha256, upload_report.actual_sha256,
        sizeof(report->sha256));
    report->upload_flags = upload_report.result_flags;
    if (status < 0 ||
        (report->upload_flags & (RSD_PACKAGE_UPLOAD_SEALED |
            RSD_PACKAGE_UPLOAD_DURABLE)) !=
            (RSD_PACKAGE_UPLOAD_SEALED | RSD_PACKAGE_UPLOAD_DURABLE)) {
        report->storage_error = status < 0 ? status : -(long)RSD_EIO;
        cleanup_upload(sink.handle, report);
        if (upload_report.actual_bytes != request->expected_bytes) {
            return RSD_PACKAGE_FETCH_LENGTH;
        }
        if (!equal_bytes(upload_report.actual_sha256,
                request->expected_sha256,
                sizeof(upload_report.actual_sha256))) {
            return RSD_PACKAGE_FETCH_DIGEST;
        }
        return RSD_PACKAGE_FETCH_UPLOAD_SEAL;
    }
    report->durable = true;
    return RSD_PACKAGE_FETCH_OK;
}

const char *rsd_package_fetch_status_string(
    enum rsd_package_fetch_status status
)
{
    static const char *const names[] = {
        "ok", "invalid package fetch argument", "temporary file open failed",
        "HTTPS fetch failed", "temporary file write failed",
        "temporary file close failed", "download length mismatch",
        "download digest mismatch", "download durability barrier failed",
        "staged-file publish failed", "package upload open failed",
        "package upload seal failed"
    };
    return (unsigned)status < sizeof(names) / sizeof(names[0]) ?
        names[status] : "unknown package fetch status";
}
