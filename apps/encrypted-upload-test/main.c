/* SPDX-License-Identifier: GPL-3.0-only */
#include <openrfs/package_upload.h>
#include <openrfs/runtime.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int data_probe(void)
{
    char readback[12] = {0};
    long file = openrfs_file_open(OPENRFS_VOLUME_DATA, "NCHK.TXT",
        OPENRFS_OPEN_READ | OPENRFS_OPEN_WRITE |
        OPENRFS_OPEN_CREATE | OPENRFS_OPEN_TRUNCATE);
    if (file < 0 || openrfs_file_write((openrfs_handle_t)file,
            "amber\n", 6U) != 6 ||
        openrfs_handle_close((openrfs_handle_t)file) != 0)
        return 10;
    file = openrfs_file_open(OPENRFS_VOLUME_DATA, "NCHK.TXT",
        OPENRFS_OPEN_WRITE | OPENRFS_OPEN_APPEND);
    if (file < 0 || openrfs_file_write((openrfs_handle_t)file,
            "blue\n", 5U) != 5 ||
        openrfs_handle_close((openrfs_handle_t)file) != 0)
        return 11;
    file = openrfs_file_open(OPENRFS_VOLUME_DATA, "NCHK.TXT",
        OPENRFS_OPEN_READ);
    if (file < 0 || openrfs_file_read((openrfs_handle_t)file,
            readback, sizeof(readback)) != 11 ||
        memcmp(readback, "amber\nblue\n", 11U) != 0 ||
        openrfs_handle_close((openrfs_handle_t)file) != 0)
        return 12;
    if (openrfs_path_truncate(OPENRFS_VOLUME_DATA, "NCHK.TXT", 5U) != 0 ||
        openrfs_path_rename(OPENRFS_VOLUME_DATA, "NCHK.TXT",
            "NREN.TXT") != 0)
        return 13;
    file = openrfs_file_open(OPENRFS_VOLUME_DATA, "NREN.TXT",
        OPENRFS_OPEN_READ);
    if (file < 0 || openrfs_file_read((openrfs_handle_t)file,
            readback, sizeof(readback)) != 5 ||
        memcmp(readback, "amber", 5U) != 0 ||
        openrfs_handle_close((openrfs_handle_t)file) != 0 ||
        openrfs_path_unlink(OPENRFS_VOLUME_DATA, "NREN.TXT") != 0)
        return 14;
    puts("OPENRFS NATIVE DATA PASS");
    return 0;
}

int main(void)
{
    static const uint8_t payload[] = "OPENRFS_UPLOAD_SENTINEL_3924";
    static const uint8_t digest[32] = {
        0x33, 0xb1, 0x0b, 0x58, 0x9d, 0x99, 0x42, 0xae,
        0x73, 0x2c, 0x00, 0x73, 0x21, 0x99, 0xed, 0x0a,
        0x8c, 0xa9, 0x37, 0xb4, 0x05, 0x57, 0x5e, 0xa9,
        0x36, 0xe7, 0x5b, 0x64, 0xbe, 0xea, 0x03, 0xdc,
    };
    const int probe = data_probe();
    if (probe != 0) return probe;
    const long opened = openrfs_package_upload_open();
    if (opened < 0) return 1;
    puts("OPENRFS UPLOAD opened");
    const openrfs_handle_t handle = (openrfs_handle_t)opened;
    if (openrfs_package_upload_write(handle, payload,
            sizeof(payload) - 1U) != (long)(sizeof(payload) - 1U)) {
        (void)openrfs_package_upload_close(handle);
        return 2;
    }
    puts("OPENRFS UPLOAD written");
    struct openrfs_package_upload_report report;
    if (openrfs_package_upload_seal(handle, sizeof(payload) - 1U,
            digest, &report) < 0 ||
        report.actual_bytes != sizeof(payload) - 1U ||
        report.result_flags != (OPENRFS_PACKAGE_UPLOAD_SEALED |
            OPENRFS_PACKAGE_UPLOAD_DURABLE)) {
        (void)openrfs_package_upload_close(handle);
        return 3;
    }
    puts("OPENRFS UPLOAD sealed");
    if (openrfs_package_upload_close(handle) < 0) return 4;
    puts("OPENRFS UPLOAD PASS");
    return 0;
}
