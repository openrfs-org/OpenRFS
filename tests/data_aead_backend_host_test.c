/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <openrfs/data_aead_backend.h>
#include <openrfs/random.h>

#define FAKE_FILES 80U
#define FAKE_HANDLES 16U
#define FAKE_FILE_BYTES 5000U

struct fake_file {
    char path[OPENRFSFS_MAX_PATH];
    uint8_t bytes[FAKE_FILE_BYTES];
    size_t size;
    bool present;
    bool directory;
};

struct fake_handle {
    unsigned file;
    size_t position;
    bool used;
};

static struct fake_file files[FAKE_FILES];
static struct fake_handle handles[FAKE_HANDLES];
static size_t write_budget;
static bool fail_rename_after;
static uint8_t nonce_seed;
static unsigned fault_step;
static unsigned step_count;

static bool cut_after_change(void)
{
    ++step_count;
    return step_count == fault_step;
}

static bool check(bool condition, const char *message)
{
    if (!condition) fprintf(stderr, "Data backend: %s\n", message);
    return condition;
}

static struct fake_file *find_file(const char *path)
{
    for (unsigned at = 0U; at < FAKE_FILES; ++at)
        if (files[at].present && strcmp(files[at].path, path) == 0)
            return &files[at];
    return NULL;
}

static struct fake_file *create_file(const char *path, bool directory)
{
    if (find_file(path) != NULL) return NULL;
    for (unsigned at = 0U; at < FAKE_FILES; ++at)
        if (!files[at].present) {
            memset(&files[at], 0, sizeof(files[at]));
            strcpy(files[at].path, path);
            files[at].present = true;
            files[at].directory = directory;
            return &files[at];
        }
    return NULL;
}

static enum openrfsfs_status fake_stat(enum openrfsfs_volume volume,
    const char *path, struct openrfsfs_stat *stat)
{
    if (volume != OPENRFSFS_VOLUME_DATA) return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    struct fake_file *file = find_file(path);
    if (file == NULL) return OPENRFSFS_STATUS_NOT_FOUND;
    memset(stat, 0, sizeof(*stat));
    stat->size = file->size;
    stat->directory = file->directory;
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status fake_mkdir(enum openrfsfs_volume volume,
    const char *path)
{
    if (volume != OPENRFSFS_VOLUME_DATA) return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    if (create_file(path, true) == NULL) return OPENRFSFS_STATUS_EXISTS;
    return cut_after_change() ? OPENRFSFS_STATUS_IO : OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status fake_create(enum openrfsfs_volume volume,
    const char *path, uint16_t mode)
{
    (void)mode;
    if (volume != OPENRFSFS_VOLUME_DATA) return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    if (create_file(path, false) == NULL) return OPENRFSFS_STATUS_EXISTS;
    return cut_after_change() ? OPENRFSFS_STATUS_IO : OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status fake_open(enum openrfsfs_volume volume,
    const char *path, enum openrfsfs_access access, openrfsfs_handle *handle)
{
    (void)access;
    if (volume != OPENRFSFS_VOLUME_DATA) return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    struct fake_file *file = find_file(path);
    if (file == NULL) return OPENRFSFS_STATUS_NOT_FOUND;
    if (file->directory) return OPENRFSFS_STATUS_IS_DIRECTORY;
    for (unsigned at = 0U; at < FAKE_HANDLES; ++at)
        if (!handles[at].used) {
            handles[at].used = true;
            handles[at].file = (unsigned)(file - files);
            handles[at].position = 0U;
            *handle = at + 1U;
            return OPENRFSFS_STATUS_OK;
        }
    return OPENRFSFS_STATUS_NO_HANDLES;
}

static enum openrfsfs_status fake_open_options(enum openrfsfs_volume volume,
    const char *path, enum openrfsfs_access access, uint8_t flags,
    uint16_t mode, openrfsfs_handle *handle, struct openrfsfs_stat *stat)
{
    if ((flags & (OPENRFSFS_OPEN_CREATE | OPENRFSFS_OPEN_EXCLUSIVE)) !=
            (OPENRFSFS_OPEN_CREATE | OPENRFSFS_OPEN_EXCLUSIVE))
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    enum openrfsfs_status status = fake_create(volume, path, mode);
    if (status == OPENRFSFS_STATUS_OK)
        status = fake_open(volume, path, access, handle);
    if (status == OPENRFSFS_STATUS_OK)
        status = fake_stat(volume, path, stat);
    return status;
}

static enum openrfsfs_status fake_close(openrfsfs_handle handle)
{
    if (handle == 0U || handle > FAKE_HANDLES ||
            !handles[handle - 1U].used) return OPENRFSFS_STATUS_STALE_HANDLE;
    handles[handle - 1U].used = false;
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status fake_fstat(openrfsfs_handle handle,
    struct openrfsfs_stat *stat)
{
    if (handle == 0U || handle > FAKE_HANDLES ||
            !handles[handle - 1U].used) return OPENRFSFS_STATUS_STALE_HANDLE;
    memset(stat, 0, sizeof(*stat));
    stat->size = files[handles[handle - 1U].file].size;
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status fake_pread(openrfsfs_handle handle,
    uint8_t *to, size_t bytes, uint64_t offset, size_t *got)
{
    *got = 0U;
    if (handle == 0U || handle > FAKE_HANDLES ||
            !handles[handle - 1U].used) return OPENRFSFS_STATUS_STALE_HANDLE;
    const struct fake_file *file = &files[handles[handle - 1U].file];
    if (offset > file->size) return OPENRFSFS_STATUS_OK;
    const size_t count = bytes < file->size - (size_t)offset ?
        bytes : file->size - (size_t)offset;
    memcpy(to, file->bytes + (size_t)offset, count);
    *got = count;
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status fake_write(openrfsfs_handle handle,
    const uint8_t *from, size_t bytes, size_t *written)
{
    *written = 0U;
    if (handle == 0U || handle > FAKE_HANDLES ||
            !handles[handle - 1U].used) return OPENRFSFS_STATUS_STALE_HANDLE;
    struct fake_handle *opened = &handles[handle - 1U];
    struct fake_file *file = &files[opened->file];
    if (write_budget == 0U) return OPENRFSFS_STATUS_FULL;
    size_t count = bytes < write_budget ? bytes : write_budget;
    if (count > FAKE_FILE_BYTES - opened->position)
        count = FAKE_FILE_BYTES - opened->position;
    memcpy(file->bytes + opened->position, from, count);
    opened->position += count;
    if (file->size < opened->position) file->size = opened->position;
    write_budget -= count;
    *written = count;
    return cut_after_change() ? OPENRFSFS_STATUS_IO : OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status fake_unlink(enum openrfsfs_volume volume,
    const char *path)
{
    if (volume != OPENRFSFS_VOLUME_DATA) return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    struct fake_file *file = find_file(path);
    if (file == NULL) return OPENRFSFS_STATUS_NOT_FOUND;
    file->present = false;
    return cut_after_change() ? OPENRFSFS_STATUS_IO : OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status fake_rename(enum openrfsfs_volume volume,
    const char *source, const char *destination)
{
    if (volume != OPENRFSFS_VOLUME_DATA) return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    struct fake_file *file = find_file(source);
    if (file == NULL) return OPENRFSFS_STATUS_NOT_FOUND;
    if (find_file(destination) != NULL) return OPENRFSFS_STATUS_EXISTS;
    strcpy(file->path, destination);
    if (fail_rename_after) {
        fail_rename_after = false;
        return OPENRFSFS_STATUS_IO;
    }
    return cut_after_change() ? OPENRFSFS_STATUS_IO : OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status fake_sync(enum openrfsfs_volume volume)
{
    if (volume != OPENRFSFS_VOLUME_DATA)
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    return cut_after_change() ? OPENRFSFS_STATUS_IO : OPENRFSFS_STATUS_OK;
}

enum random_status random_bytes(void *destination, size_t length)
{
    for (size_t at = 0U; at < length; ++at)
        ((uint8_t *)destination)[at] = (uint8_t)(nonce_seed + at);
    ++nonce_seed;
    return RANDOM_STATUS_OK;
}

static bool no_open_handles(void)
{
    for (unsigned at = 0U; at < FAKE_HANDLES; ++at)
        if (handles[at].used) return false;
    return true;
}

static int run_case(bool ext4_style)
{
    memset(files, 0, sizeof(files));
    memset(handles, 0, sizeof(handles));
    write_budget = SIZE_MAX;
    fail_rename_after = false;
    nonce_seed = 1U;
    fault_step = 0U;
    step_count = 0U;
    struct vfs_backend_ops backend = {
        .sync = fake_sync,
        .open = fake_open,
        .close = fake_close,
        .pread = fake_pread,
        .write = fake_write,
        .stat_path = fake_stat,
        .mkdir = fake_mkdir,
        .rename = fake_rename,
        .unlink = fake_unlink,
        .create = fake_create,
    };
    if (ext4_style) {
        backend.open_options = fake_open_options;
        backend.fstat = fake_fstat;
        backend.lstat_path = fake_stat;
    }
    uint8_t key[DATA_AEAD_KEY_BYTES];
    uint8_t wrong_key[DATA_AEAD_KEY_BYTES];
    uint8_t plain[DATA_AEAD_CHUNK_BYTES] = {0};
    uint8_t nonce[DATA_AEAD_NONCE_BYTES] = {0};
    uint8_t workspace[DATA_AEAD_REWRITE_WORKSPACE_BYTES];
    char binding[DATA_AEAD_PATH_MAX + 1U];
    char segment_path[DATA_AEAD_PATH_MAX + 1U];
    char manifest_path[DATA_AEAD_PATH_MAX + 1U];
    struct data_aead_manifest candidate = {0};
    struct data_aead_manifest published;
    struct data_aead_manifest loaded;
    unsigned slot = 99U;
    for (size_t at = 0U; at < sizeof(key); ++at) {
        key[at] = (uint8_t)(at + 1U);
        wrong_key[at] = (uint8_t)(at + 2U);
    }
    candidate.stable_id[0] = 11U;
    candidate.generation = 1U;
    candidate.plaintext_bytes = 5U;
    candidate.segment_count = 1U;
    candidate.segments[0].revision_id[0] = 31U;
    candidate.segments[0].plaintext_bytes = 5U;
    candidate.segments[0].generation = 1U;
    if (!check(data_aead_segment_binding(candidate.stable_id, 0U,
            binding) == DATA_AEAD_OK &&
            data_aead_segment_storage_path(key,
                candidate.segments[0].revision_id,
                segment_path) == DATA_AEAD_OK,
            "segment paths")) return 1;
    struct fake_file *segment = create_file(segment_path, false);
    if (!check(segment != NULL, "create encrypted segment")) return 1;
    nonce[0] = 1U;
    memcpy(plain, "hello", 5U);
    if (!check(data_aead_make_header_v2(key, binding, 5U,
            candidate.stable_id, candidate.segments[0].revision_id,
            1U, segment->bytes) == DATA_AEAD_OK &&
            data_aead_seal_chunk(key, segment->bytes, 0U, nonce,
                plain, 5U, segment->bytes + DATA_AEAD_HEADER_BYTES) ==
                DATA_AEAD_OK,
            "seal physical segment")) return 1;
    segment->size = DATA_AEAD_HEADER_BYTES + DATA_AEAD_SEALED_CHUNK_BYTES;
    if (!check(data_aead_backend_publish_manifest(&backend,
            OPENRFSFS_VOLUME_DATA, key, "DOC/NOTE.TXT", &candidate,
            workspace, sizeof(workspace), &published, &slot) ==
            DATA_AEAD_OK && slot == 0U &&
            published.plaintext_bytes == 5U && no_open_handles(),
            "first backend publication")) return 1;
    if (!check(data_aead_manifest_storage_path(key, "DOC/NOTE.TXT",
            0U, manifest_path) == DATA_AEAD_OK &&
            find_file(manifest_path) != NULL &&
            find_file(manifest_path)->size == DATA_AEAD_MANIFEST_BYTES &&
            memcmp(segment->bytes, "hello", 5U) != 0,
            "raw storage contains ciphertext and manifest")) return 1;
    if (!check(data_aead_backend_load_manifest(&backend,
            OPENRFSFS_VOLUME_DATA, key, "DOC/NOTE.TXT", workspace,
            sizeof(workspace), &loaded, &slot) == DATA_AEAD_OK &&
            loaded.plaintext_bytes == 5U && slot == 0U &&
            no_open_handles(), "load verifies complete content")) return 1;
    candidate.generation = 2U;
    if (!check(data_aead_backend_publish_manifest(&backend,
            OPENRFSFS_VOLUME_DATA, key, "DOC/NOTE.TXT", &candidate,
            workspace, sizeof(workspace), &published, &slot) ==
            DATA_AEAD_OK && slot == 1U && no_open_handles(),
            "second backend publication")) return 1;
    candidate.generation = 3U;
    write_budget = 10U;
    if (!check(data_aead_backend_publish_manifest(&backend,
            OPENRFSFS_VOLUME_DATA, key, "DOC/NOTE.TXT", &candidate,
            workspace, sizeof(workspace), &published, &slot) ==
            DATA_AEAD_IO && no_open_handles(),
            "disk full during temp write refuses")) return 1;
    write_budget = SIZE_MAX;
    if (!check(data_aead_backend_publish_manifest(&backend,
            OPENRFSFS_VOLUME_DATA, key, "DOC/NOTE.TXT", &candidate,
            workspace, sizeof(workspace), &published, &slot) ==
            DATA_AEAD_OK && slot == 0U && no_open_handles(),
            "disk-full retry keeps old key and publishes complete file"))
        return 1;
    candidate.generation = 4U;
    fail_rename_after = true;
    if (!check(data_aead_backend_publish_manifest(&backend,
            OPENRFSFS_VOLUME_DATA, key, "DOC/NOTE.TXT", &candidate,
            workspace, sizeof(workspace), &published, &slot) ==
            DATA_AEAD_IO && no_open_handles(),
            "rename cut reports failure") ||
        !check(data_aead_backend_publish_manifest(&backend,
            OPENRFSFS_VOLUME_DATA, key, "DOC/NOTE.TXT", &candidate,
            workspace, sizeof(workspace), &published, &slot) ==
            DATA_AEAD_OK && slot == 1U && no_open_handles(),
            "rename cut resumes at complete generation")) return 1;
    candidate.generation = 5U;
    segment->bytes[segment->size - 1U] ^= 1U;
    if (!check(data_aead_backend_load_manifest(&backend,
            OPENRFSFS_VOLUME_DATA, key, "DOC/NOTE.TXT", workspace,
            sizeof(workspace), &loaded, &slot) ==
            DATA_AEAD_AUTHENTICATION && no_open_handles(),
            "load refuses tampered content")) return 1;
    if (!check(data_aead_backend_publish_manifest(&backend,
            OPENRFSFS_VOLUME_DATA, key, "DOC/NOTE.TXT", &candidate,
            workspace, sizeof(workspace), &published, &slot) ==
            DATA_AEAD_AUTHENTICATION && no_open_handles(),
            "tampered segment blocks publication")) return 1;
    segment->bytes[segment->size - 1U] ^= 1U;
    candidate.generation = 1U;
    if (!check(data_aead_backend_publish_manifest(&backend,
            OPENRFSFS_VOLUME_DATA, wrong_key, "DOC/NOTE.TXT", &candidate,
            workspace, sizeof(workspace), &published, &slot) ==
            DATA_AEAD_IO && no_open_handles(),
            "wrong key never accepts plaintext or another storage root"))
        return 1;
    struct fake_file *legacy = create_file("DOC/LEGACY.TXT", false);
    if (!check(legacy != NULL, "create legacy source")) return 1;
    memcpy(legacy->bytes, "secret", 6U);
    legacy->size = 6U;
    write_budget = 100U;
    if (!check(data_aead_backend_migrate_plain(&backend,
            OPENRFSFS_VOLUME_DATA, key, "DOC/LEGACY.TXT", workspace,
            sizeof(workspace), &loaded) == DATA_AEAD_IO &&
            find_file("DOC/LEGACY.TXT") != NULL && no_open_handles(),
            "disk full retains plaintext source")) return 1;
    write_budget = SIZE_MAX;
    if (!check(data_aead_backend_migrate_plain(&backend,
            OPENRFSFS_VOLUME_DATA, key, "DOC/LEGACY.TXT", workspace,
            sizeof(workspace), &loaded) == DATA_AEAD_OK &&
            loaded.plaintext_bytes == 6U &&
            find_file("DOC/LEGACY.TXT") == NULL && no_open_handles(),
            "disk-full migration resumes")) return 1;
    if (!check(data_aead_segment_storage_path(key,
            loaded.segments[0].revision_id, segment_path) == DATA_AEAD_OK &&
            find_file(segment_path) != NULL &&
            memcmp(find_file(segment_path)->bytes, "secret", 6U) != 0 &&
            data_aead_backend_load_manifest(&backend,
                OPENRFSFS_VOLUME_DATA, key, "DOC/LEGACY.TXT", workspace,
                sizeof(workspace), &loaded, &slot) == DATA_AEAD_OK,
            "migrated content is authenticated and raw bytes differ"))
        return 1;
    uint8_t readback[8] = {0};
    size_t read_bytes = 0U;
    memset(readback, 0xa5, sizeof(readback));
    if (!check(data_aead_backend_read_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, "DOC/NONE.TXT", 0U, readback,
            sizeof(readback), workspace, sizeof(workspace), &read_bytes) ==
            DATA_AEAD_NOT_FOUND && read_bytes == 0U &&
            memcmp(readback, "\0\0\0\0\0\0\0\0", sizeof(readback)) == 0,
            "missing manifest never falls back to plaintext")) return 1;
    if (!check(data_aead_backend_read_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, "DOC/LEGACY.TXT", 0U, readback,
            sizeof(readback), workspace, sizeof(workspace), &read_bytes) ==
            DATA_AEAD_OK && read_bytes == 6U &&
            memcmp(readback, "secret", 6U) == 0 && no_open_handles(),
            "backend read decrypts migrated content")) return 1;
    memset(readback, 0U, sizeof(readback));
    if (!check(data_aead_backend_read_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, "DOC/LEGACY.TXT", 2U, readback,
            3U, workspace, sizeof(workspace), &read_bytes) == DATA_AEAD_OK &&
            read_bytes == 3U && memcmp(readback, "cre", 3U) == 0 &&
            data_aead_backend_read_file(&backend,
                OPENRFSFS_VOLUME_DATA, key, "DOC/LEGACY.TXT", 6U,
                readback, sizeof(readback), workspace,
                sizeof(workspace), &read_bytes) == DATA_AEAD_OK &&
            read_bytes == 0U && no_open_handles(),
            "bounded and EOF reads")) return 1;
    struct fake_file *encrypted = find_file(segment_path);
    encrypted->bytes[encrypted->size - 1U] ^= 1U;
    memset(readback, 0xa5, sizeof(readback));
    if (!check(data_aead_backend_read_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, "DOC/LEGACY.TXT", 0U, readback,
            sizeof(readback), workspace, sizeof(workspace), &read_bytes) ==
            DATA_AEAD_AUTHENTICATION && read_bytes == 0U &&
            memcmp(readback, "\0\0\0\0\0\0\0\0", sizeof(readback)) == 0 &&
            no_open_handles(), "tampered read clears output")) return 1;
    encrypted->bytes[encrypted->size - 1U] ^= 1U;
    legacy = create_file("DOC/CUT.TXT", false);
    if (!check(legacy != NULL, "create cut source")) return 1;
    memcpy(legacy->bytes, "restore", 7U);
    legacy->size = 7U;
    fail_rename_after = true;
    if (!check(data_aead_backend_migrate_plain(&backend,
            OPENRFSFS_VOLUME_DATA, key, "DOC/CUT.TXT", workspace,
            sizeof(workspace), &loaded) == DATA_AEAD_IO &&
            find_file("DOC/CUT.TXT") != NULL && no_open_handles(),
            "publication cut keeps source")) return 1;
    if (!check(data_aead_backend_migrate_plain(&backend,
            OPENRFSFS_VOLUME_DATA, key, "DOC/CUT.TXT", workspace,
            sizeof(workspace), &loaded) == DATA_AEAD_OK &&
            loaded.plaintext_bytes == 7U &&
            find_file("DOC/CUT.TXT") == NULL && no_open_handles(),
            "publication cut resumes without rewriting")) return 1;
    if (!check(create_file("DOC/EMPTY.TXT", false) != NULL,
            "create empty legacy source")) return 1;
    const enum data_aead_status empty_result =
        data_aead_backend_migrate_plain(&backend, OPENRFSFS_VOLUME_DATA,
            key, "DOC/EMPTY.TXT", workspace, sizeof(workspace), &loaded);
    if (!check(empty_result == DATA_AEAD_OK &&
            loaded.segment_count == 0U && loaded.plaintext_bytes == 0U &&
            find_file("DOC/EMPTY.TXT") == NULL && no_open_handles(),
            "empty migration publishes before source removal"))
        return 1;
    return 0;
}

static int run_migration_cuts(bool ext4_style)
{
    uint8_t key[DATA_AEAD_KEY_BYTES];
    uint8_t workspace[DATA_AEAD_REWRITE_WORKSPACE_BYTES];
    for (size_t at = 0U; at < sizeof(key); ++at)
        key[at] = (uint8_t)(at + 1U);
    for (unsigned cut = 1U; cut < 40U; ++cut) {
        memset(files, 0, sizeof(files));
        memset(handles, 0, sizeof(handles));
        write_budget = SIZE_MAX;
        fail_rename_after = false;
        nonce_seed = 1U;
        fault_step = 0U;
        step_count = 0U;
        struct vfs_backend_ops backend = {
            .sync = fake_sync,
            .open = fake_open,
            .close = fake_close,
            .pread = fake_pread,
            .write = fake_write,
            .stat_path = fake_stat,
            .mkdir = fake_mkdir,
            .rename = fake_rename,
            .unlink = fake_unlink,
            .create = fake_create,
        };
        if (ext4_style) {
            backend.open_options = fake_open_options;
            backend.fstat = fake_fstat;
            backend.lstat_path = fake_stat;
        }
        struct fake_file *source = create_file("DOC/CUTS.TXT", false);
        if (!check(source != NULL, "cut fixture source")) return 1;
        memcpy(source->bytes, "private", 7U);
        source->size = 7U;
        struct data_aead_manifest migrated;
        fault_step = cut;
        const enum data_aead_status first = data_aead_backend_migrate_plain(
            &backend, OPENRFSFS_VOLUME_DATA, key, "DOC/CUTS.TXT",
            workspace, sizeof(workspace), &migrated);
        if (!check(first == DATA_AEAD_IO || first == DATA_AEAD_OK,
                "cut returns I/O or complete migration") ||
            !check(no_open_handles(), "cut closes every handle"))
            return 1;
        fault_step = 0U;
        if (!check(data_aead_backend_migrate_plain(&backend,
                OPENRFSFS_VOLUME_DATA, key, "DOC/CUTS.TXT", workspace,
                sizeof(workspace), &migrated) == DATA_AEAD_OK &&
                migrated.plaintext_bytes == 7U &&
                find_file("DOC/CUTS.TXT") == NULL && no_open_handles(),
                "each cut resumes to a complete encrypted file"))
            return 1;
        char segment_path[DATA_AEAD_PATH_MAX + 1U];
        if (!check(data_aead_segment_storage_path(key,
                migrated.segments[0].revision_id, segment_path) ==
                DATA_AEAD_OK && find_file(segment_path) != NULL &&
                memcmp(find_file(segment_path)->bytes, "private", 7U) != 0,
                "cut retry leaves ciphertext at physical path")) return 1;
        if (first == DATA_AEAD_OK) {
            if (!check(cut > 10U, "cut sweep reached publication"))
                return 1;
            break;
        }
        if (!check(cut < 39U, "cut sweep reaches success")) return 1;
    }
    return 0;
}

int main(void)
{
    if (run_case(false) != 0 || run_case(true) != 0 ||
            run_migration_cuts(false) != 0 ||
            run_migration_cuts(true) != 0) return 1;
    puts("Data AEAD FAT32/ext4 backend adapter, migration cuts, disk full, tamper and wrong-key controls passed");
    return 0;
}
