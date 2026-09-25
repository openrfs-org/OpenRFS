/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <openrfs/data_aead_backend.h>
#include <openrfs/data_namespace.h>
#include <openrfs/data_namespace_backend.h>
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
    legacy = create_file("DOC/NAME.TXT", false);
    if (!check(legacy != NULL, "create named migration source")) return 1;
    memcpy(legacy->bytes, "hidden", 6U);
    legacy->size = 6U;
    uint8_t named_id[DATA_AEAD_ID_BYTES];
    uint8_t unused_revision[DATA_AEAD_ID_BYTES];
    char named_binding[DATA_AEAD_PATH_MAX + 1U];
    if (!check(data_aead_migration_ids(key, "DOC/NAME.TXT", 0U,
            named_id, unused_revision) == DATA_AEAD_OK &&
            data_ns_file_binding(named_id, named_binding) == DATA_NS_OK,
            "derive name-free file binding")) return 1;
    if (!check(data_aead_backend_migrate_plain_as(&backend,
            OPENRFSFS_VOLUME_DATA, key, "DOC/NAME.TXT", named_binding,
            workspace, sizeof(workspace), &loaded) == DATA_AEAD_OK &&
            memcmp(loaded.stable_id, named_id, sizeof(named_id)) == 0 &&
            find_file("DOC/NAME.TXT") != NULL && no_open_handles(),
            "named migration keeps source until namespace commit"))
        return 1;
    memset(readback, 0U, sizeof(readback));
    if (!check(data_aead_backend_read_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, named_binding, 0U, readback,
            sizeof(readback), workspace, sizeof(workspace), &read_bytes) ==
            DATA_AEAD_OK && read_bytes == 6U &&
            memcmp(readback, "hidden", 6U) == 0 &&
            data_aead_backend_migrate_plain_as(&backend,
                OPENRFSFS_VOLUME_DATA, key, "DOC/NAME.TXT",
                named_binding, workspace, sizeof(workspace),
                &loaded) == DATA_AEAD_OK &&
            find_file("DOC/NAME.TXT") != NULL && no_open_handles(),
            "name-free migration reads and resumes without early removal"))
        return 1;
    legacy->bytes[0] ^= 1U;
    if (!check(data_aead_backend_migrate_plain_as(&backend,
            OPENRFSFS_VOLUME_DATA, key, "DOC/NAME.TXT", named_binding,
            workspace, sizeof(workspace), &loaded) == DATA_AEAD_CONFLICT &&
            loaded.generation == 0U &&
            find_file("DOC/NAME.TXT") != NULL && no_open_handles(),
            "changed plaintext source cannot be retired on retry"))
        return 1;
    legacy->bytes[0] ^= 1U;
    if (!check(data_aead_backend_migrate_plain_as(&backend,
            OPENRFSFS_VOLUME_DATA, key, "DOC/NAME.TXT", named_binding,
            workspace, sizeof(workspace), &loaded) == DATA_AEAD_OK &&
            loaded.plaintext_bytes == 6U && no_open_handles(),
            "unchanged source still resumes")) return 1;
    struct data_aead_manifest namespace_file = {0};
    namespace_file.stable_id[0] = 71U;
    namespace_file.generation = 1U;
    if (!check(data_aead_backend_publish_manifest(&backend,
            OPENRFSFS_VOLUME_DATA, key, "NAMESPACE", &namespace_file,
            workspace, sizeof(workspace), &published, &slot) ==
            DATA_AEAD_OK && no_open_handles(),
            "empty namespace file publishes")) return 1;
    const uint8_t event[] = {'e', 'v', 'e', 'n', 't'};
    if (!check(data_aead_backend_append_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, "NAMESPACE", 1U, event,
            sizeof(event), workspace, sizeof(workspace), &published) ==
            DATA_AEAD_OK && published.generation == 2U &&
            published.plaintext_bytes == sizeof(event) &&
            no_open_handles(), "encrypted append publishes")) return 1;
    memset(readback, 0U, sizeof(readback));
    if (!check(data_aead_backend_read_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, "NAMESPACE", 0U, readback,
            sizeof(readback), workspace, sizeof(workspace), &read_bytes) ==
            DATA_AEAD_OK && read_bytes == sizeof(event) &&
            memcmp(readback, event, sizeof(event)) == 0,
            "append content decrypts")) return 1;
    struct data_aead_backend_reader reader;
    if (!check(data_aead_backend_reader_open(&backend,
            OPENRFSFS_VOLUME_DATA, key, "NAMESPACE", workspace,
            sizeof(workspace), &reader) == DATA_AEAD_OK &&
            reader.manifest.generation == 2U,
            "retained reader opens an authenticated revision")) return 1;
    uint8_t wrong_reader_key[DATA_AEAD_KEY_BYTES];
    memcpy(wrong_reader_key, key, sizeof(key));
    wrong_reader_key[0] ^= 1U;
    memset(readback, 0xa5, sizeof(readback));
    if (!check(data_aead_backend_reader_read(&reader,
            wrong_reader_key, 0U, readback, sizeof(readback),
            workspace, sizeof(workspace), &read_bytes) ==
            DATA_AEAD_AUTHENTICATION && read_bytes == 0U &&
            memcmp(readback, "\0\0\0\0\0\0\0\0", sizeof(readback)) == 0,
            "retained reader refuses a wrong key")) return 1;
    if (!check(data_aead_backend_append_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, "NAMESPACE", 1U, event,
            sizeof(event), workspace, sizeof(workspace), &published) ==
            DATA_AEAD_OK && published.generation == 2U &&
            published.plaintext_bytes == sizeof(event),
            "append retry does not duplicate content")) return 1;
    if (!check(data_aead_backend_append_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, "NAMESPACE", 1U,
            event + 1U, sizeof(event) - 1U, workspace,
            sizeof(workspace), &published) == DATA_AEAD_CONFLICT &&
            published.generation == 0U && reader.active,
            "matching suffix is not a matching append revision")) return 1;
    const uint8_t next_event[] = {'n', 'e', 'x', 't'};
    if (!check(data_aead_backend_append_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, "NAMESPACE", 2U, next_event,
            sizeof(next_event), workspace, sizeof(workspace),
            &published) == DATA_AEAD_OK &&
            published.generation == 3U &&
            published.plaintext_bytes ==
                sizeof(event) + sizeof(next_event),
            "append rewrites existing segment")) return 1;
    memset(readback, 0U, sizeof(readback));
    if (!check(data_aead_backend_reader_read(&reader, key, 0U,
            readback, sizeof(readback), workspace, sizeof(workspace),
            &read_bytes) == DATA_AEAD_OK &&
            read_bytes == sizeof(event) &&
            memcmp(readback, event, sizeof(event)) == 0 &&
            data_aead_backend_reader_close(&reader) == DATA_AEAD_OK &&
            no_open_handles(),
            "retained handle reads its old revision after append"))
        return 1;
    return 0;
}

static int run_migration_cuts(bool ext4_style, bool preserve_source)
{
    uint8_t key[DATA_AEAD_KEY_BYTES];
    uint8_t workspace[DATA_AEAD_REWRITE_WORKSPACE_BYTES];
    for (size_t at = 0U; at < sizeof(key); ++at)
        key[at] = (uint8_t)(at + 1U);
    uint8_t stable_id[DATA_AEAD_ID_BYTES];
    uint8_t revision_id[DATA_AEAD_ID_BYTES];
    char binding[DATA_AEAD_PATH_MAX + 1U];
    if (!check(data_aead_migration_ids(key, "DOC/CUTS.TXT", 0U,
            stable_id, revision_id) == DATA_AEAD_OK &&
            data_ns_file_binding(stable_id, binding) == DATA_NS_OK,
            "cut fixture has a stable file binding")) return 1;
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
        const enum data_aead_status first = preserve_source ?
            data_aead_backend_migrate_plain_as(&backend,
                OPENRFSFS_VOLUME_DATA, key, "DOC/CUTS.TXT",
                binding, workspace, sizeof(workspace), &migrated) :
            data_aead_backend_migrate_plain(&backend,
                OPENRFSFS_VOLUME_DATA, key, "DOC/CUTS.TXT",
                workspace, sizeof(workspace), &migrated);
        if (!check(first == DATA_AEAD_IO || first == DATA_AEAD_OK,
                "cut returns I/O or complete migration") ||
            !check(no_open_handles(), "cut closes every handle"))
            return 1;
        fault_step = 0U;
        if (!check((preserve_source ?
                data_aead_backend_migrate_plain_as(&backend,
                    OPENRFSFS_VOLUME_DATA, key, "DOC/CUTS.TXT",
                    binding, workspace, sizeof(workspace), &migrated) :
                data_aead_backend_migrate_plain(&backend,
                    OPENRFSFS_VOLUME_DATA, key, "DOC/CUTS.TXT",
                    workspace, sizeof(workspace), &migrated)) ==
                    DATA_AEAD_OK &&
                migrated.plaintext_bytes == 7U &&
                (find_file("DOC/CUTS.TXT") != NULL) ==
                    preserve_source && no_open_handles(),
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

static int run_namespace_backend_case(bool ext4_style)
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
    uint8_t workspace[DATA_AEAD_REWRITE_WORKSPACE_BYTES];
    for (size_t at = 0U; at < sizeof(key); ++at) {
        key[at] = (uint8_t)(at + 1U);
        wrong_key[at] = (uint8_t)(at + 2U);
    }
    uint64_t generation = 0U;
    struct data_ns_entry entries[4];
    struct data_ns_entry scratch[4];
    struct data_ns_state state = {
        entries, 4U, 0U, {0}, ext4_style
    };
    if (!check(data_ns_backend_create_empty(&backend,
            OPENRFSFS_VOLUME_DATA, key, workspace, sizeof(workspace),
            &generation) == DATA_NS_OK && generation == 1U &&
            data_ns_backend_load(&backend, OPENRFSFS_VOLUME_DATA,
                key, workspace, sizeof(workspace), &state,
                &generation) == DATA_NS_OK && generation == 1U &&
            state.count == 0U && no_open_handles(),
            "authenticated empty namespace opens")) return 1;
    struct data_ns_event event = {0};
    event.operation = DATA_NS_CREATE;
    event.kind = DATA_NS_DIRECTORY;
    event.mode = 0700U;
    memcpy(event.parent_id, state.root_id, DATA_AEAD_ID_BYTES);
    event.child_id[0] = 7U;
    strcpy(event.name, "PRIVATE");
    if (!check(data_ns_backend_append(&backend,
            OPENRFSFS_VOLUME_DATA, key, workspace, sizeof(workspace),
            &state, scratch, generation, &event, &generation) ==
            DATA_NS_OK && generation == 2U && state.count == 1U &&
            data_ns_backend_load(&backend, OPENRFSFS_VOLUME_DATA,
                key, workspace, sizeof(workspace), &state,
                &generation) == DATA_NS_OK && generation == 2U &&
            data_ns_find(&state, state.root_id, "PRIVATE") != NULL &&
            no_open_handles(), "namespace append replays from ciphertext"))
        return 1;
    event.operation = DATA_NS_RENAME;
    memcpy(event.target_parent_id, state.root_id, DATA_AEAD_ID_BYTES);
    strcpy(event.target_name, "HIDDEN");
    if (!check(data_ns_backend_append(&backend,
            OPENRFSFS_VOLUME_DATA, key, workspace, sizeof(workspace),
            &state, scratch, generation, &event, &generation) ==
            DATA_NS_OK && generation == 3U &&
            data_ns_backend_load(&backend, OPENRFSFS_VOLUME_DATA,
                key, workspace, sizeof(workspace), &state,
                &generation) == DATA_NS_OK &&
            data_ns_find(&state, state.root_id, "PRIVATE") == NULL &&
            data_ns_find(&state, state.root_id, "HIDDEN") != NULL,
            "encrypted namespace rename replays")) return 1;
    event.operation = DATA_NS_DELETE;
    strcpy(event.name, "HIDDEN");
    memset(event.target_parent_id, 0, DATA_AEAD_ID_BYTES);
    event.target_name[0] = '\0';
    if (!check(data_ns_backend_append(&backend,
            OPENRFSFS_VOLUME_DATA, key, workspace, sizeof(workspace),
            &state, scratch, generation, &event, &generation) ==
            DATA_NS_OK && generation == 4U &&
            data_ns_backend_load(&backend, OPENRFSFS_VOLUME_DATA,
                key, workspace, sizeof(workspace), &state,
                &generation) == DATA_NS_OK && state.count == 0U &&
            no_open_handles(),
            "encrypted namespace delete replays")) return 1;
    struct data_aead_manifest manifest;
    unsigned slot = 0U;
    char segment_path[DATA_AEAD_PATH_MAX + 1U];
    if (!check(data_aead_backend_load_manifest(&backend,
            OPENRFSFS_VOLUME_DATA, key, "NAMESPACE", workspace,
            sizeof(workspace), &manifest, &slot) == DATA_AEAD_OK &&
            data_aead_segment_storage_path(key,
                manifest.segments[0].revision_id, segment_path) ==
                DATA_AEAD_OK, "namespace segment locates")) return 1;
    struct fake_file *sealed = find_file(segment_path);
    if (!check(sealed != NULL &&
            memcmp(sealed->bytes, "PRIVATE", 7U) != 0,
            "namespace segment has no plaintext prefix")) return 1;
    sealed->bytes[sealed->size - 1U] ^= 1U;
    if (!check(data_ns_backend_load(&backend, OPENRFSFS_VOLUME_DATA,
            key, workspace, sizeof(workspace), &state,
            &generation) == DATA_NS_FORMAT && state.count == 0U &&
            generation == 0U && no_open_handles(),
            "tampered namespace never exposes partial entries")) return 1;
    sealed->bytes[sealed->size - 1U] ^= 1U;
    if (!check(data_ns_backend_load(&backend, OPENRFSFS_VOLUME_DATA,
            wrong_key, workspace, sizeof(workspace), &state,
            &generation) == DATA_NS_NOT_FOUND && state.count == 0U &&
            generation == 0U && no_open_handles(),
            "wrong namespace key refuses")) return 1;
    return 0;
}

static int run_append_cuts(bool ext4_style)
{
    uint8_t key[DATA_AEAD_KEY_BYTES];
    uint8_t workspace[DATA_AEAD_REWRITE_WORKSPACE_BYTES];
    const uint8_t addition[] = {'e', 'v', 'e', 'n', 't'};
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
        struct data_aead_manifest empty_manifest = {0};
        struct data_aead_manifest published;
        unsigned slot = 0U;
        empty_manifest.stable_id[0] = 91U;
        empty_manifest.generation = 1U;
        if (!check(data_aead_backend_publish_manifest(&backend,
                OPENRFSFS_VOLUME_DATA, key, "NAMESPACE",
                &empty_manifest, workspace, sizeof(workspace),
                &published, &slot) == DATA_AEAD_OK,
                "append cut fixture publishes empty file")) return 1;
        step_count = 0U;
        fault_step = cut;
        const enum data_aead_status first =
            data_aead_backend_append_file(&backend,
                OPENRFSFS_VOLUME_DATA, key, "NAMESPACE", 1U,
                addition, sizeof(addition), workspace,
                sizeof(workspace), &published);
        if (!check(first == DATA_AEAD_IO || first == DATA_AEAD_OK,
                "append cut returns I/O or success") ||
            !check(no_open_handles(), "append cut closes every handle"))
            return 1;
        fault_step = 0U;
        if (!check(data_aead_backend_append_file(&backend,
                OPENRFSFS_VOLUME_DATA, key, "NAMESPACE", 1U,
                addition, sizeof(addition), workspace,
                sizeof(workspace), &published) == DATA_AEAD_OK &&
                published.generation == 2U &&
                published.plaintext_bytes == sizeof(addition) &&
                no_open_handles(),
                "append cut resumes without duplicate bytes")) return 1;
        uint8_t readback[8] = {0};
        size_t read_bytes = 0U;
        if (!check(data_aead_backend_read_file(&backend,
                OPENRFSFS_VOLUME_DATA, key, "NAMESPACE", 0U, readback,
                sizeof(readback), workspace, sizeof(workspace),
                &read_bytes) == DATA_AEAD_OK &&
                read_bytes == sizeof(addition) &&
                memcmp(readback, addition, sizeof(addition)) == 0,
                "append cut leaves complete content")) return 1;
        if (first == DATA_AEAD_OK) {
            if (!check(cut > 10U, "append cut sweep reached commit"))
                return 1;
            break;
        }
        if (!check(cut < 39U, "append cut sweep reaches success"))
            return 1;
    }
    return 0;
}

static int run_rewrite_case(bool ext4_style)
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
    uint8_t workspace[DATA_AEAD_REWRITE_WORKSPACE_BYTES];
    for (size_t at = 0U; at < sizeof(key); ++at)
        key[at] = (uint8_t)(at + 1U);
    struct data_aead_manifest empty_manifest = {0};
    struct data_aead_manifest published;
    unsigned slot = 0U;
    empty_manifest.stable_id[0] = 88U;
    empty_manifest.generation = 1U;
    if (!check(data_aead_backend_publish_manifest(&backend,
            OPENRFSFS_VOLUME_DATA, key, "FIL/REWRITE",
            &empty_manifest, workspace, sizeof(workspace),
            &published, &slot) == DATA_AEAD_OK,
            "rewrite fixture publishes empty file")) return 1;
    if (!check(data_aead_backend_rewrite_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, "FIL/REWRITE", 1U, 6U,
            0U, (const uint8_t *)"secret", 6U, workspace,
            sizeof(workspace), &published) == DATA_AEAD_OK &&
            published.generation == 2U &&
            published.plaintext_bytes == 6U,
            "rewrite creates encrypted file")) return 1;
    if (!check(data_aead_backend_rewrite_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, "FIL/REWRITE", 2U, 6U,
            2U, (const uint8_t *)"XYZ", 3U, workspace,
            sizeof(workspace), &published) == DATA_AEAD_OK &&
            published.generation == 3U,
            "partial rewrite publishes")) return 1;
    uint8_t readback[16] = {0};
    size_t got = 0U;
    if (!check(data_aead_backend_read_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, "FIL/REWRITE", 0U,
            readback, sizeof(readback), workspace, sizeof(workspace),
            &got) == DATA_AEAD_OK && got == 6U &&
            memcmp(readback, "seXYZt", 6U) == 0,
            "partial rewrite preserves other bytes")) return 1;
    if (!check(data_aead_backend_rewrite_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, "FIL/REWRITE", 2U, 6U,
            2U, (const uint8_t *)"XYZ", 3U, workspace,
            sizeof(workspace), &published) == DATA_AEAD_OK &&
            published.generation == 3U,
            "rewrite retry recognizes the same revision")) return 1;
    if (!check(data_aead_backend_rewrite_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, "FIL/REWRITE", 2U, 6U,
            2U, (const uint8_t *)"XYQ", 3U, workspace,
            sizeof(workspace), &published) == DATA_AEAD_CONFLICT &&
            published.generation == 0U,
            "different rewrite cannot borrow retry generation")) return 1;
    write_budget = 100U;
    if (!check(data_aead_backend_rewrite_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, "FIL/REWRITE", 3U, 6U,
            0U, (const uint8_t *)"A", 1U, workspace,
            sizeof(workspace), &published) == DATA_AEAD_IO &&
            published.generation == 0U && no_open_handles(),
            "full disk refuses rewrite before publication")) return 1;
    write_budget = SIZE_MAX;
    memset(readback, 0U, sizeof(readback));
    if (!check(data_aead_backend_read_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, "FIL/REWRITE", 0U,
            readback, sizeof(readback), workspace, sizeof(workspace),
            &got) == DATA_AEAD_OK && got == 6U &&
            memcmp(readback, "seXYZt", 6U) == 0,
            "full disk leaves old revision readable")) return 1;
    if (!check(data_aead_backend_rewrite_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, "FIL/REWRITE", 3U, 10U,
            9U, (const uint8_t *)"Z", 1U, workspace,
            sizeof(workspace), &published) == DATA_AEAD_OK &&
            published.generation == 4U,
            "sparse growth publishes zeros")) return 1;
    memset(readback, 0xa5, sizeof(readback));
    if (!check(data_aead_backend_read_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, "FIL/REWRITE", 0U,
            readback, sizeof(readback), workspace, sizeof(workspace),
            &got) == DATA_AEAD_OK && got == 10U &&
            memcmp(readback, "seXYZt\0\0\0Z", 10U) == 0,
            "sparse encrypted bytes read as zero")) return 1;
    if (!check(data_aead_backend_rewrite_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, "FIL/REWRITE", 4U, 3U,
            0U, NULL, 0U, workspace, sizeof(workspace),
            &published) == DATA_AEAD_OK &&
            published.generation == 5U &&
            published.plaintext_bytes == 3U,
            "truncate publishes shorter revision")) return 1;
    memset(readback, 0U, sizeof(readback));
    if (!check(data_aead_backend_read_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, "FIL/REWRITE", 0U,
            readback, sizeof(readback), workspace, sizeof(workspace),
            &got) == DATA_AEAD_OK && got == 3U &&
            memcmp(readback, "seX", 3U) == 0,
            "truncate keeps prefix")) return 1;
    if (!check(data_aead_backend_rewrite_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, "FIL/REWRITE", 5U, 0U,
            0U, NULL, 0U, workspace, sizeof(workspace),
            &published) == DATA_AEAD_OK &&
            published.generation == 6U &&
            published.segment_count == 0U && no_open_handles(),
            "truncate to zero drops referenced segments")) return 1;
    return 0;
}

static int run_rewrite_cuts(bool ext4_style)
{
    uint8_t key[DATA_AEAD_KEY_BYTES];
    uint8_t workspace[DATA_AEAD_REWRITE_WORKSPACE_BYTES];
    for (size_t at = 0U; at < sizeof(key); ++at)
        key[at] = (uint8_t)(at + 1U);
    for (unsigned cut = 1U; cut < 45U; ++cut) {
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
        struct data_aead_manifest empty_manifest = {0};
        struct data_aead_manifest published;
        unsigned slot = 0U;
        empty_manifest.stable_id[0] = 89U;
        empty_manifest.generation = 1U;
        if (!check(data_aead_backend_publish_manifest(&backend,
                OPENRFSFS_VOLUME_DATA, key, "FIL/CUT",
                &empty_manifest, workspace, sizeof(workspace),
                &published, &slot) == DATA_AEAD_OK &&
                data_aead_backend_rewrite_file(&backend,
                    OPENRFSFS_VOLUME_DATA, key, "FIL/CUT", 1U, 4U,
                    0U, (const uint8_t *)"base", 4U, workspace,
                    sizeof(workspace), &published) == DATA_AEAD_OK,
                "rewrite cut fixture has old revision")) return 1;
        step_count = 0U;
        fault_step = cut;
        const enum data_aead_status first =
            data_aead_backend_rewrite_file(&backend,
                OPENRFSFS_VOLUME_DATA, key, "FIL/CUT", 2U, 4U,
                1U, (const uint8_t *)"XY", 2U, workspace,
                sizeof(workspace), &published);
        if (!check(first == DATA_AEAD_IO || first == DATA_AEAD_OK,
                "rewrite cut returns I/O or complete commit") ||
            !check(no_open_handles(), "rewrite cut closes handles"))
            return 1;
        fault_step = 0U;
        uint8_t readback[4] = {0};
        size_t got = 0U;
        if (!check(data_aead_backend_read_file(&backend,
                OPENRFSFS_VOLUME_DATA, key, "FIL/CUT", 0U,
                readback, sizeof(readback), workspace,
                sizeof(workspace), &got) == DATA_AEAD_OK &&
                got == 4U &&
                (memcmp(readback, "base", 4U) == 0 ||
                 memcmp(readback, "bXYe", 4U) == 0),
                "cut exposes one complete revision")) return 1;
        if (!check(data_aead_backend_rewrite_file(&backend,
                OPENRFSFS_VOLUME_DATA, key, "FIL/CUT", 2U, 4U,
                1U, (const uint8_t *)"XY", 2U, workspace,
                sizeof(workspace), &published) == DATA_AEAD_OK &&
                published.generation == 3U && no_open_handles(),
                "rewrite cut retries without losing the old revision"))
            return 1;
        if (first == DATA_AEAD_OK) {
            if (!check(cut > 10U, "rewrite cut reached commit"))
                return 1;
            break;
        }
        if (!check(cut < 44U, "rewrite cut sweep reaches success"))
            return 1;
    }
    return 0;
}

static int run_namespace_cuts(bool ext4_style)
{
    uint8_t key[DATA_AEAD_KEY_BYTES];
    uint8_t workspace[DATA_AEAD_REWRITE_WORKSPACE_BYTES];
    for (size_t at = 0U; at < sizeof(key); ++at)
        key[at] = (uint8_t)(at + 1U);
    for (unsigned cut = 1U; cut < 45U; ++cut) {
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
        uint64_t generation = 0U;
        struct data_ns_entry entries[2];
        struct data_ns_entry scratch[2];
        struct data_ns_state state = {
            entries, 2U, 0U, {0}, ext4_style
        };
        if (!check(data_ns_backend_create_empty(&backend,
                OPENRFSFS_VOLUME_DATA, key, workspace,
                sizeof(workspace), &generation) == DATA_NS_OK &&
                data_ns_backend_load(&backend, OPENRFSFS_VOLUME_DATA,
                    key, workspace, sizeof(workspace), &state,
                    &generation) == DATA_NS_OK && generation == 1U,
                "namespace cut fixture loads empty revision")) return 1;
        struct data_ns_event event = {0};
        event.operation = DATA_NS_CREATE;
        event.kind = DATA_NS_FILE;
        event.mode = 0600U;
        memcpy(event.parent_id, state.root_id, DATA_AEAD_ID_BYTES);
        event.child_id[0] = 61U;
        strcpy(event.name, "PRIVATE.TXT");
        step_count = 0U;
        fault_step = cut;
        const enum data_ns_status first = data_ns_backend_append(&backend,
            OPENRFSFS_VOLUME_DATA, key, workspace, sizeof(workspace),
            &state, scratch, generation, &event, &generation);
        if (!check(first == DATA_NS_IO || first == DATA_NS_OK,
                "namespace cut returns I/O or complete commit") ||
            !check(no_open_handles(), "namespace cut closes handles"))
            return 1;
        fault_step = 0U;
        if (!check(data_ns_backend_load(&backend,
                OPENRFSFS_VOLUME_DATA, key, workspace,
                sizeof(workspace), &state, &generation) == DATA_NS_OK &&
                ((generation == 1U && state.count == 0U) ||
                 (generation == 2U && state.count == 1U)) &&
                no_open_handles(),
                "namespace cut replays only complete log")) return 1;
        if (generation == 1U &&
                !check(data_ns_backend_append(&backend,
                    OPENRFSFS_VOLUME_DATA, key, workspace,
                    sizeof(workspace), &state, scratch, generation,
                    &event, &generation) == DATA_NS_OK,
                    "namespace cut retries event")) return 1;
        if (!check(generation == 2U && state.count == 1U &&
                data_ns_find(&state, state.root_id,
                    "PRIVATE.TXT") != NULL && no_open_handles(),
                "namespace cut ends with one event")) return 1;
        if (first == DATA_NS_OK) {
            if (!check(cut > 10U, "namespace cut reached commit"))
                return 1;
            break;
        }
        if (!check(cut < 44U, "namespace cut sweep reaches success"))
            return 1;
    }
    return 0;
}

static int run_boundary_case(bool ext4_style)
{
#if DATA_AEAD_SEGMENT_BYTES == DATA_AEAD_CHUNK_BYTES
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
    uint8_t workspace[DATA_AEAD_REWRITE_WORKSPACE_BYTES];
    uint8_t original[DATA_AEAD_CHUNK_BYTES - 2U];
    memset(original, 'A', sizeof(original));
    for (size_t at = 0U; at < sizeof(key); ++at)
        key[at] = (uint8_t)(at + 1U);
    struct data_aead_manifest empty_manifest = {0};
    struct data_aead_manifest published;
    unsigned slot = 0U;
    empty_manifest.stable_id[0] = 90U;
    empty_manifest.generation = 1U;
    if (!check(data_aead_backend_publish_manifest(&backend,
            OPENRFSFS_VOLUME_DATA, key, "FIL/BOUNDARY",
            &empty_manifest, workspace, sizeof(workspace),
            &published, &slot) == DATA_AEAD_OK &&
            data_aead_backend_rewrite_file(&backend,
                OPENRFSFS_VOLUME_DATA, key, "FIL/BOUNDARY", 1U,
                sizeof(original), 0U, original, sizeof(original),
                workspace, sizeof(workspace), &published) == DATA_AEAD_OK,
            "boundary fixture has first segment")) return 1;
    if (!check(data_aead_backend_append_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, "FIL/BOUNDARY", 2U,
            (const uint8_t *)"BCDE", 4U, workspace,
            sizeof(workspace), &published) == DATA_AEAD_OK &&
            published.segment_count == 2U &&
            published.segments[0].plaintext_bytes ==
                DATA_AEAD_SEGMENT_BYTES &&
            published.segments[1].plaintext_bytes == 2U,
            "append crosses a segment boundary")) return 1;
    uint8_t readback[6] = {0};
    size_t got = 0U;
    if (!check(data_aead_backend_read_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, "FIL/BOUNDARY",
            DATA_AEAD_SEGMENT_BYTES - 4U, readback,
            sizeof(readback), workspace, sizeof(workspace),
            &got) == DATA_AEAD_OK && got == 6U &&
            memcmp(readback, "AABCDE", 6U) == 0,
            "read crosses a segment boundary")) return 1;
    if (!check(data_aead_backend_rewrite_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, "FIL/BOUNDARY", 3U,
            DATA_AEAD_SEGMENT_BYTES - 1U, 0U, NULL, 0U,
            workspace, sizeof(workspace), &published) == DATA_AEAD_OK &&
            published.segment_count == 1U,
            "truncate drops second segment")) return 1;
    if (!check(data_aead_backend_rewrite_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, "FIL/BOUNDARY", 4U,
            DATA_AEAD_SEGMENT_BYTES + 2U,
            DATA_AEAD_SEGMENT_BYTES - 1U,
            (const uint8_t *)"XYZ", 3U, workspace,
            sizeof(workspace), &published) == DATA_AEAD_OK &&
            published.segment_count == 2U,
            "partial rewrite crosses a segment boundary")) return 1;
    memset(readback, 0U, sizeof(readback));
    if (!check(data_aead_backend_read_file(&backend,
            OPENRFSFS_VOLUME_DATA, key, "FIL/BOUNDARY",
            DATA_AEAD_SEGMENT_BYTES - 3U, readback, 5U,
            workspace, sizeof(workspace), &got) == DATA_AEAD_OK &&
            got == 5U && memcmp(readback, "ABXYZ", 5U) == 0 &&
            no_open_handles(),
            "cross-segment rewrite preserves both sides")) return 1;
#else
    (void)ext4_style;
#endif
    return 0;
}

int main(void)
{
    if (run_case(false) != 0 || run_case(true) != 0 ||
            run_namespace_backend_case(false) != 0 ||
            run_namespace_backend_case(true) != 0 ||
            run_migration_cuts(false, false) != 0 ||
            run_migration_cuts(true, false) != 0 ||
            run_migration_cuts(false, true) != 0 ||
            run_migration_cuts(true, true) != 0 ||
            run_append_cuts(false) != 0 ||
            run_append_cuts(true) != 0 ||
            run_rewrite_case(false) != 0 ||
            run_rewrite_case(true) != 0 ||
            run_rewrite_cuts(false) != 0 ||
            run_rewrite_cuts(true) != 0 ||
            run_namespace_cuts(false) != 0 ||
            run_namespace_cuts(true) != 0 ||
            run_boundary_case(false) != 0 ||
            run_boundary_case(true) != 0) return 1;
    puts("Data AEAD FAT32/ext4 backend migration, namespace replay, append and rewrite cuts, disk full, tamper and wrong-key controls passed");
    return 0;
}
