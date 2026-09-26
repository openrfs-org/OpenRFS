/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/data_encrypted_migration.h>
#include <openrfs/data_namespace_backend.h>

#include "../../vendor/monocypher/src/monocypher.h"

#define INVENTORY_ITEMS 384U
#define INVENTORY_RECORD_BYTES 384U
#define INVENTORY_LIST_ENTRIES OPENRFSFS_MAX_LIST_ENTRIES

struct inventory_item {
    char path[DATA_AEAD_PATH_MAX + 1U];
    struct openrfsfs_stat stat;
    uint8_t digest[32];
    enum data_ns_kind kind;
};

static struct inventory_item items[INVENTORY_ITEMS];
static struct inventory_item prior_items[INVENTORY_ITEMS];
static struct openrfsfs_list_entry listed[INVENTORY_ITEMS];
static struct data_ns_entry namespace_entries[INVENTORY_ITEMS];
static struct data_ns_entry namespace_scratch[INVENTORY_ITEMS];
static uint8_t workspace[DATA_AEAD_REWRITE_WORKSPACE_BYTES];
static const char inventory_binding[] = "INVENTORY";
static const uint8_t inventory_magic[4] = {'O', 'D', 'I', '1'};
static const uint8_t inventory_domain[] = "OpenRFS Data inventory v1";

static void wipe(void *memory, size_t bytes)
{
    volatile uint8_t *to = memory;
    for (size_t at = 0U; at < bytes; ++at) to[at] = 0U;
}

static void copy_bytes(void *destination, const void *source, size_t bytes)
{
    uint8_t *to = destination;
    const uint8_t *from = source;
    for (size_t at = 0U; at < bytes; ++at) to[at] = from[at];
}

static size_t path_length(const char *path)
{
    if (path == NULL) return DATA_AEAD_PATH_MAX + 1U;
    size_t length = 0U;
    while (length <= DATA_AEAD_PATH_MAX && path[length] != '\0')
        ++length;
    return length;
}

static bool same_text(const char *left, const char *right)
{
    const size_t left_bytes = path_length(left);
    if (left_bytes > DATA_AEAD_PATH_MAX ||
            left_bytes != path_length(right)) return false;
    for (size_t at = 0U; at <= left_bytes; ++at)
        if (left[at] != right[at]) return false;
    return true;
}

static bool same_physical_name(const struct vfs_backend_ops *physical,
    const char *left, const char *right)
{
    if (physical->case_sensitive) return same_text(left, right);
    const size_t length = path_length(left);
    if (length > DATA_AEAD_PATH_MAX ||
            length != path_length(right)) return false;
    for (size_t at = 0U; at < length; ++at) {
        char a = left[at];
        char b = right[at];
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        if (a != b) return false;
    }
    return true;
}

static void put_u16(uint8_t *to, uint16_t value)
{
    to[0] = (uint8_t)value;
    to[1] = (uint8_t)(value >> 8U);
}

static void put_u32(uint8_t *to, uint32_t value)
{
    for (unsigned at = 0U; at < 4U; ++at)
        to[at] = (uint8_t)(value >> (8U * at));
}

static void put_u64(uint8_t *to, uint64_t value)
{
    for (unsigned at = 0U; at < 8U; ++at)
        to[at] = (uint8_t)(value >> (8U * at));
}

static uint16_t get_u16(const uint8_t *from)
{
    return (uint16_t)from[0] | (uint16_t)((uint16_t)from[1] << 8U);
}

static uint32_t get_u32(const uint8_t *from)
{
    uint32_t value = 0U;
    for (unsigned at = 0U; at < 4U; ++at)
        value |= (uint32_t)from[at] << (8U * at);
    return value;
}

static uint64_t get_u64(const uint8_t *from)
{
    uint64_t value = 0U;
    for (unsigned at = 0U; at < 8U; ++at)
        value |= (uint64_t)from[at] << (8U * at);
    return value;
}

static enum openrfsfs_status map_aead(enum data_aead_status status)
{
    if (status == DATA_AEAD_OK) return OPENRFSFS_STATUS_OK;
    if (status == DATA_AEAD_NOT_FOUND) return OPENRFSFS_STATUS_NOT_FOUND;
    if (status == DATA_AEAD_RANGE) return OPENRFSFS_STATUS_RANGE;
    if (status == DATA_AEAD_CONFLICT) return OPENRFSFS_STATUS_BUSY;
    if (status == DATA_AEAD_ARGUMENT)
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    return status == DATA_AEAD_IO || status == DATA_AEAD_ENTROPY ?
        OPENRFSFS_STATUS_IO : OPENRFSFS_STATUS_CORRUPT;
}

static enum openrfsfs_status map_namespace(enum data_ns_status status)
{
    if (status == DATA_NS_OK) return OPENRFSFS_STATUS_OK;
    if (status == DATA_NS_NOT_FOUND) return OPENRFSFS_STATUS_NOT_FOUND;
    if (status == DATA_NS_FULL) return OPENRFSFS_STATUS_FULL;
    if (status == DATA_NS_IO) return OPENRFSFS_STATUS_IO;
    if (status == DATA_NS_ARGUMENT)
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    return OPENRFSFS_STATUS_CORRUPT;
}

static enum openrfsfs_status storage_root(
    const uint8_t key[DATA_AEAD_KEY_BYTES], char root[9])
{
    char path[DATA_AEAD_PATH_MAX + 1U];
    const enum openrfsfs_status result = map_aead(
        data_aead_manifest_storage_path(key, "NAMESPACE", 0U,
            path));
    if (result == OPENRFSFS_STATUS_OK) {
        copy_bytes(root, path, 8U);
        root[8] = '\0';
    }
    wipe(path, sizeof(path));
    return result;
}

enum openrfsfs_status data_encrypted_migration_preflight(
    const struct vfs_backend_ops *physical,
    const uint8_t key[DATA_AEAD_KEY_BYTES])
{
    if (physical == NULL || physical->stat_path == NULL || key == NULL)
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    char root[9];
    enum openrfsfs_status result = storage_root(key, root);
    if (result == OPENRFSFS_STATUS_OK) {
        struct openrfsfs_stat stat;
        const enum openrfsfs_status found = physical->lstat_path != NULL ?
            physical->lstat_path(OPENRFSFS_VOLUME_DATA,
                root, &stat) :
            physical->stat_path(OPENRFSFS_VOLUME_DATA,
                root, &stat);
        result = found == OPENRFSFS_STATUS_NOT_FOUND ?
            OPENRFSFS_STATUS_OK :
            found == OPENRFSFS_STATUS_OK ?
                OPENRFSFS_STATUS_EXISTS : found;
    }
    wipe(root, sizeof(root));
    return result;
}

static bool symlink_or_special(const struct openrfsfs_stat *stat)
{
    const uint16_t type = stat->mode & 0170000U;
    return type != 0U && type != 0040000U && type != 0100000U;
}

static enum openrfsfs_status source_stat(
    const struct vfs_backend_ops *physical, const char *path,
    struct openrfsfs_stat *stat)
{
    const enum openrfsfs_status result = physical->lstat_path != NULL ?
        physical->lstat_path(OPENRFSFS_VOLUME_DATA, path, stat) :
        physical->stat_path(OPENRFSFS_VOLUME_DATA, path, stat);
    if (result != OPENRFSFS_STATUS_OK) return result;
    if (symlink_or_special(stat) || stat->xattrs_present ||
            (!stat->directory && stat->links > 1U) ||
            stat->atime_nanos >= 1000000000U ||
            stat->mtime_nanos >= 1000000000U ||
            stat->ctime_nanos >= 1000000000U ||
            stat->atime_seconds < 0 || stat->mtime_seconds < 0 ||
            stat->ctime_seconds < 0)
        return OPENRFSFS_STATUS_CORRUPT;
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status hash_source(
    const struct vfs_backend_ops *physical,
    const uint8_t key[DATA_AEAD_KEY_BYTES], const char *path,
    uint64_t expected_size, uint8_t digest[32])
{
    openrfsfs_handle handle = 0U;
    enum openrfsfs_status result = physical->open(OPENRFSFS_VOLUME_DATA,
        path, OPENRFSFS_ACCESS_READ, &handle);
    if (result != OPENRFSFS_STATUS_OK) return result;
    struct openrfsfs_stat stat;
    if (physical->fstat != NULL &&
            (physical->fstat(handle, &stat) !=
                OPENRFSFS_STATUS_OK || stat.directory ||
             stat.size != expected_size))
        result = OPENRFSFS_STATUS_CORRUPT;
    crypto_blake2b_ctx context;
    crypto_blake2b_keyed_init(&context, 32U, key,
        DATA_AEAD_KEY_BYTES);
    crypto_blake2b_update(&context, inventory_domain,
        sizeof(inventory_domain) - 1U);
    uint8_t chunk[DATA_AEAD_CHUNK_BYTES];
    for (uint64_t offset = 0U; result == OPENRFSFS_STATUS_OK &&
            offset < expected_size;) {
        size_t amount = sizeof(chunk);
        if (expected_size - offset < amount)
            amount = (size_t)(expected_size - offset);
        size_t got = 0U;
        result = physical->pread(handle, chunk, amount,
            offset, &got);
        if (result == OPENRFSFS_STATUS_OK && got != amount)
            result = OPENRFSFS_STATUS_IO;
        if (result == OPENRFSFS_STATUS_OK)
            crypto_blake2b_update(&context, chunk, amount);
        offset += amount;
    }
    if (result == OPENRFSFS_STATUS_OK)
        crypto_blake2b_final(&context, digest);
    if (physical->close(handle) != OPENRFSFS_STATUS_OK)
        result = OPENRFSFS_STATUS_IO;
    if (result != OPENRFSFS_STATUS_OK) wipe(digest, 32U);
    wipe(chunk, sizeof(chunk));
    wipe(&context, sizeof(context));
    return result;
}

static enum openrfsfs_status credential_directory(
    const struct vfs_backend_ops *physical)
{
    size_t count = 0U;
    enum openrfsfs_status result = physical->list(
        OPENRFSFS_VOLUME_DATA, "OPENRFS", listed,
        INVENTORY_LIST_ENTRIES, &count);
    if (result != OPENRFSFS_STATUS_OK) return result;
    for (size_t at = 0U; at < count; ++at)
        if (listed[at].directory ||
                (!same_physical_name(physical, listed[at].name,
                    "LOGIN.DAT") &&
                 !same_physical_name(physical, listed[at].name,
                    "LOGIN.NEW") &&
                 !same_physical_name(physical, listed[at].name,
                    "LOGIN.V2A") &&
                 !same_physical_name(physical, listed[at].name,
                    "LOGIN.V2B")))
            return OPENRFSFS_STATUS_CORRUPT;
    wipe(listed, sizeof(listed));
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status scan_directory(
    const struct vfs_backend_ops *physical,
    const uint8_t key[DATA_AEAD_KEY_BYTES],
    const char *directory, const char root[9], size_t *count)
{
    size_t found = 0U;
    enum openrfsfs_status result = OPENRFSFS_STATUS_OK;
    if (physical->directory_open != NULL &&
            physical->directory_read != NULL &&
            physical->directory_close != NULL) {
        openrfsfs_handle handle = 0U;
        result = physical->directory_open(OPENRFSFS_VOLUME_DATA,
            directory, &handle);
        while (result == OPENRFSFS_STATUS_OK) {
            struct openrfsfs_list_entry next;
            bool present = false;
            result = physical->directory_read(handle, &next, &present);
            if (result != OPENRFSFS_STATUS_OK || !present) break;
            if (found >= INVENTORY_ITEMS) {
                result = OPENRFSFS_STATUS_FULL;
                break;
            }
            listed[found++] = next;
        }
        if (handle != 0U && physical->directory_close(handle) !=
                OPENRFSFS_STATUS_OK)
            result = OPENRFSFS_STATUS_IO;
    } else result = physical->list(OPENRFSFS_VOLUME_DATA,
        directory, listed, INVENTORY_LIST_ENTRIES, &found);
    if (result != OPENRFSFS_STATUS_OK) return result;
    for (size_t at = 0U; at < found; ++at) {
        const char *name = listed[at].name;
        const size_t name_bytes = path_length(name);
        const size_t parent_bytes = same_text(directory, ".") ?
            0U : path_length(directory);
        if (name_bytes == 0U || name_bytes > DATA_NS_NAME_BYTES ||
                (parent_bytes != 0U &&
                 parent_bytes > DATA_AEAD_PATH_MAX) ||
                parent_bytes + (parent_bytes != 0U ? 1U : 0U) +
                    name_bytes > DATA_AEAD_PATH_MAX ||
                same_text(name, ".") || same_text(name, ".."))
            return OPENRFSFS_STATUS_NAME_TOO_LONG;
        if (parent_bytes == 0U &&
                same_physical_name(physical, name, "OPENRFS")) {
            continue;
        }
        if (parent_bytes == 0U &&
                same_physical_name(physical, name, root))
            continue;
        if (*count >= INVENTORY_ITEMS) return OPENRFSFS_STATUS_FULL;
        struct inventory_item *item = &items[*count];
        wipe(item, sizeof(*item));
        if (parent_bytes != 0U) {
            copy_bytes(item->path, directory, parent_bytes);
            item->path[parent_bytes] = '/';
        }
        copy_bytes(item->path + parent_bytes +
            (parent_bytes != 0U ? 1U : 0U), name, name_bytes);
        for (size_t previous = 0U; previous < *count; ++previous)
            if (same_text(items[previous].path, item->path))
                return OPENRFSFS_STATUS_CORRUPT;
        result = source_stat(physical, item->path, &item->stat);
        if (result != OPENRFSFS_STATUS_OK) return result;
        if (item->stat.directory != listed[at].directory)
            return OPENRFSFS_STATUS_CORRUPT;
        item->kind = item->stat.directory ? DATA_NS_DIRECTORY :
            DATA_NS_FILE;
        item->stat.mode &= 07777U;
        if (item->kind == DATA_NS_DIRECTORY) item->stat.size = 0U;
        if (item->kind == DATA_NS_FILE) {
            if (item->stat.size >
                    DATA_AEAD_SEGMENT_BYTES * DATA_AEAD_SEGMENTS_MAX)
                return OPENRFSFS_STATUS_RANGE;
            result = hash_source(physical, key, item->path,
                item->stat.size, item->digest);
            if (result != OPENRFSFS_STATUS_OK) return result;
        }
        ++*count;
    }
    wipe(listed, sizeof(listed));
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status scan_plain_tree(
    const struct vfs_backend_ops *physical,
    const uint8_t key[DATA_AEAD_KEY_BYTES],
    const char root[9], size_t *count)
{
    *count = 0U;
    wipe(items, sizeof(items));
    enum openrfsfs_status result = scan_directory(physical, key,
        ".", root, count);
    if (result == OPENRFSFS_STATUS_OK) {
        struct openrfsfs_stat credential_stat;
        const enum openrfsfs_status found = source_stat(physical,
            "OPENRFS", &credential_stat);
        if (found == OPENRFSFS_STATUS_OK && credential_stat.directory)
            result = credential_directory(physical);
        else if (found != OPENRFSFS_STATUS_NOT_FOUND)
            result = OPENRFSFS_STATUS_CORRUPT;
    }
    for (size_t at = 0U; result == OPENRFSFS_STATUS_OK &&
            at < *count; ++at)
        if (items[at].kind == DATA_NS_DIRECTORY)
            result = scan_directory(physical, key,
                items[at].path, root, count);
    if (result != OPENRFSFS_STATUS_OK) {
        wipe(items, sizeof(items));
        *count = 0U;
    }
    return result;
}

static bool item_metadata_valid(const struct inventory_item *item)
{
    return item->stat.atime_seconds >= 0 &&
        item->stat.mtime_seconds >= 0 &&
        item->stat.ctime_seconds >= 0 &&
        item->stat.atime_nanos < 1000000000U &&
        item->stat.mtime_nanos < 1000000000U &&
        item->stat.ctime_nanos < 1000000000U;
}

static enum openrfsfs_status encode_item(const struct inventory_item *item,
    uint8_t record[INVENTORY_RECORD_BYTES])
{
    wipe(record, INVENTORY_RECORD_BYTES);
    const size_t length = path_length(item->path);
    if (length == 0U || length > DATA_AEAD_PATH_MAX ||
            (item->kind != DATA_NS_FILE &&
             item->kind != DATA_NS_DIRECTORY) ||
            !item_metadata_valid(item))
        return OPENRFSFS_STATUS_CORRUPT;
    copy_bytes(record, inventory_magic, sizeof(inventory_magic));
    record[4] = (uint8_t)item->kind;
    record[5] = (uint8_t)length;
    put_u64(record + 8U, item->kind == DATA_NS_FILE ?
        item->stat.size : 0U);
    copy_bytes(record + 16U, item->digest, sizeof(item->digest));
    put_u16(record + 48U, item->stat.mode & 07777U);
    record[50] = item->stat.attributes;
    put_u32(record + 52U, item->stat.uid);
    put_u32(record + 56U, item->stat.gid);
    put_u64(record + 60U, (uint64_t)item->stat.atime_seconds);
    put_u64(record + 68U, (uint64_t)item->stat.mtime_seconds);
    put_u64(record + 76U, (uint64_t)item->stat.ctime_seconds);
    put_u32(record + 84U, item->stat.atime_nanos);
    put_u32(record + 88U, item->stat.mtime_nanos);
    put_u32(record + 92U, item->stat.ctime_nanos);
    copy_bytes(record + 96U, item->path, length);
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status decode_item(
    const uint8_t record[INVENTORY_RECORD_BYTES],
    struct inventory_item *item)
{
    wipe(item, sizeof(*item));
    for (size_t at = 0U; at < sizeof(inventory_magic); ++at)
        if (record[at] != inventory_magic[at])
            return OPENRFSFS_STATUS_CORRUPT;
    const size_t length = record[5];
    if ((record[4] != DATA_NS_FILE &&
         record[4] != DATA_NS_DIRECTORY) ||
            length == 0U || record[6] != 0U || record[7] != 0U ||
            record[51] != 0U)
        return OPENRFSFS_STATUS_CORRUPT;
    for (size_t at = length; at < 256U; ++at)
        if (record[96U + at] != 0U)
            return OPENRFSFS_STATUS_CORRUPT;
    for (size_t at = 352U; at < INVENTORY_RECORD_BYTES; ++at)
        if (record[at] != 0U) return OPENRFSFS_STATUS_CORRUPT;
    item->kind = (enum data_ns_kind)record[4];
    copy_bytes(item->path, record + 96U, length);
    item->stat.size = get_u64(record + 8U);
    copy_bytes(item->digest, record + 16U, sizeof(item->digest));
    item->stat.mode = get_u16(record + 48U);
    item->stat.attributes = record[50];
    item->stat.uid = get_u32(record + 52U);
    item->stat.gid = get_u32(record + 56U);
    if (get_u64(record + 60U) > INT64_MAX ||
            get_u64(record + 68U) > INT64_MAX ||
            get_u64(record + 76U) > INT64_MAX)
        return OPENRFSFS_STATUS_CORRUPT;
    item->stat.atime_seconds = (int64_t)get_u64(record + 60U);
    item->stat.mtime_seconds = (int64_t)get_u64(record + 68U);
    item->stat.ctime_seconds = (int64_t)get_u64(record + 76U);
    item->stat.atime_nanos = get_u32(record + 84U);
    item->stat.mtime_nanos = get_u32(record + 88U);
    item->stat.ctime_nanos = get_u32(record + 92U);
    item->stat.directory = item->kind == DATA_NS_DIRECTORY;
    if (!item_metadata_valid(item) ||
            (item->kind == DATA_NS_DIRECTORY &&
             item->stat.size != 0U))
        return OPENRFSFS_STATUS_CORRUPT;
    return OPENRFSFS_STATUS_OK;
}

static bool same_item(const struct inventory_item *left,
    const struct inventory_item *right)
{
    if (!same_text(left->path, right->path) ||
            left->kind != right->kind ||
            left->stat.size != right->stat.size ||
            left->stat.mode != right->stat.mode ||
            left->stat.attributes != right->stat.attributes ||
            left->stat.uid != right->stat.uid ||
            left->stat.gid != right->stat.gid ||
            left->stat.mtime_seconds != right->stat.mtime_seconds ||
            left->stat.mtime_nanos != right->stat.mtime_nanos ||
            left->stat.ctime_seconds != right->stat.ctime_seconds ||
            left->stat.ctime_nanos != right->stat.ctime_nanos)
        return false;
    return crypto_verify32(left->digest, right->digest) == 0;
}

static void inventory_hash_begin(crypto_blake2b_ctx *context,
    const uint8_t key[DATA_AEAD_KEY_BYTES])
{
    crypto_blake2b_keyed_init(context, 32U, key,
        DATA_AEAD_KEY_BYTES);
    crypto_blake2b_update(context, inventory_domain,
        sizeof(inventory_domain) - 1U);
}

static enum openrfsfs_status read_record(
    const struct data_aead_backend_reader *reader,
    const uint8_t key[DATA_AEAD_KEY_BYTES], uint64_t index,
    uint8_t record[INVENTORY_RECORD_BYTES])
{
    size_t got = 0U;
    const enum openrfsfs_status result = map_aead(
        data_aead_backend_reader_read(reader, key,
            index * INVENTORY_RECORD_BYTES, record,
            INVENTORY_RECORD_BYTES, workspace,
            sizeof(workspace), &got));
    return result == OPENRFSFS_STATUS_OK &&
        got != INVENTORY_RECORD_BYTES ?
            OPENRFSFS_STATUS_CORRUPT : result;
}

static enum openrfsfs_status inventory_end(
    const uint8_t record[INVENTORY_RECORD_BYTES],
    uint64_t count, const uint8_t digest[32])
{
    for (size_t at = 0U; at < 4U; ++at)
        if (record[at] != inventory_magic[at])
            return OPENRFSFS_STATUS_CORRUPT;
    if (record[4] != 3U || record[5] != 0U ||
            record[6] != 0U || record[7] != 0U ||
            get_u64(record + 8U) != count ||
            crypto_verify32(record + 16U, digest) != 0)
        return OPENRFSFS_STATUS_CORRUPT;
    for (size_t at = 48U; at < INVENTORY_RECORD_BYTES; ++at)
        if (record[at] != 0U) return OPENRFSFS_STATUS_CORRUPT;
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status load_inventory(
    const struct vfs_backend_ops *physical,
    const uint8_t key[DATA_AEAD_KEY_BYTES],
    size_t *count, size_t *partial, bool *complete)
{
    *count = 0U;
    *partial = 0U;
    *complete = false;
    struct data_aead_backend_reader reader = {0};
    enum openrfsfs_status result = map_aead(
        data_aead_backend_reader_open(physical,
            OPENRFSFS_VOLUME_DATA, key, inventory_binding,
            workspace, sizeof(workspace), &reader));
    if (result != OPENRFSFS_STATUS_OK) return result;
    const uint64_t bytes = reader.manifest.plaintext_bytes;
    if (bytes % INVENTORY_RECORD_BYTES != 0U ||
            bytes > (INVENTORY_ITEMS + 1U) *
                INVENTORY_RECORD_BYTES)
        result = OPENRFSFS_STATUS_CORRUPT;
    const uint64_t records = bytes / INVENTORY_RECORD_BYTES;
    crypto_blake2b_ctx context;
    inventory_hash_begin(&context, key);
    uint8_t record[INVENTORY_RECORD_BYTES];
    for (uint64_t at = 0U; result == OPENRFSFS_STATUS_OK &&
            at < records; ++at) {
        result = read_record(&reader, key, at, record);
        if (result != OPENRFSFS_STATUS_OK) break;
        if (record[4] == 3U) {
            uint8_t digest[32];
            crypto_blake2b_final(&context, digest);
            result = at == records - 1U ?
                inventory_end(record, at, digest) :
                OPENRFSFS_STATUS_CORRUPT;
            wipe(digest, sizeof(digest));
            if (result == OPENRFSFS_STATUS_OK) {
                *complete = true;
                *count = (size_t)at;
            }
            break;
        }
        if (at >= INVENTORY_ITEMS) {
            result = OPENRFSFS_STATUS_FULL;
            break;
        }
        result = decode_item(record, &items[at]);
        if (result != OPENRFSFS_STATUS_OK) break;
        for (uint64_t previous = 0U; previous < at; ++previous)
            if (same_text(items[previous].path, items[at].path)) {
                result = OPENRFSFS_STATUS_CORRUPT;
                break;
            }
        if (result != OPENRFSFS_STATUS_OK) break;
        crypto_blake2b_update(&context, record, sizeof(record));
        *partial = (size_t)at + 1U;
    }
    if (result == OPENRFSFS_STATUS_OK && !*complete &&
            records == INVENTORY_ITEMS + 1U)
        result = OPENRFSFS_STATUS_CORRUPT;
    if (data_aead_backend_reader_close(&reader) != DATA_AEAD_OK)
        result = OPENRFSFS_STATUS_IO;
    if (result != OPENRFSFS_STATUS_OK) {
        wipe(items, sizeof(items));
        *count = 0U;
        *partial = 0U;
        *complete = false;
    }
    wipe(record, sizeof(record));
    wipe(&context, sizeof(context));
    return result;
}

static enum openrfsfs_status append_inventory_record(
    const struct vfs_backend_ops *physical,
    const uint8_t key[DATA_AEAD_KEY_BYTES],
    uint64_t *generation,
    const uint8_t record[INVENTORY_RECORD_BYTES])
{
    struct data_aead_manifest published = {0};
    const enum openrfsfs_status result = map_aead(
        data_aead_backend_append_file(physical,
            OPENRFSFS_VOLUME_DATA, key, inventory_binding,
            *generation, record, INVENTORY_RECORD_BYTES,
            workspace, sizeof(workspace), &published));
    if (result == OPENRFSFS_STATUS_OK)
        *generation = published.generation;
    wipe(&published, sizeof(published));
    return result;
}

static enum openrfsfs_status reset_inventory(
    const struct vfs_backend_ops *physical,
    const uint8_t key[DATA_AEAD_KEY_BYTES],
    uint64_t *generation)
{
    struct data_aead_manifest current = {0};
    unsigned slot = 0U;
    enum openrfsfs_status result = map_aead(
        data_aead_backend_load_manifest(physical,
            OPENRFSFS_VOLUME_DATA, key, inventory_binding,
            workspace, sizeof(workspace), &current, &slot));
    if (result == OPENRFSFS_STATUS_NOT_FOUND) {
        struct data_aead_manifest candidate = {0};
        uint8_t unused[DATA_AEAD_ID_BYTES];
        result = map_aead(data_aead_migration_ids(key,
            inventory_binding, 0U, candidate.stable_id,
            unused));
        wipe(unused, sizeof(unused));
        if (result == OPENRFSFS_STATUS_OK) {
            candidate.generation = 1U;
            result = map_aead(data_aead_backend_publish_manifest(
                physical, OPENRFSFS_VOLUME_DATA, key,
                inventory_binding, &candidate, workspace,
                sizeof(workspace), &current, &slot));
        }
        wipe(&candidate, sizeof(candidate));
    } else if (result == OPENRFSFS_STATUS_OK) {
        struct data_aead_manifest published = {0};
        result = map_aead(data_aead_backend_rewrite_file(physical,
            OPENRFSFS_VOLUME_DATA, key, inventory_binding,
            current.generation, 0U, 0U, NULL, 0U,
            workspace, sizeof(workspace), &published));
        if (result == OPENRFSFS_STATUS_OK) current = published;
        wipe(&published, sizeof(published));
    }
    if (result == OPENRFSFS_STATUS_OK) *generation = current.generation;
    wipe(&current, sizeof(current));
    return result;
}

static enum openrfsfs_status write_inventory(
    const struct vfs_backend_ops *physical,
    const uint8_t key[DATA_AEAD_KEY_BYTES], size_t count)
{
    uint64_t generation = 0U;
    enum openrfsfs_status result = reset_inventory(physical,
        key, &generation);
    if (result != OPENRFSFS_STATUS_OK) return result;
    crypto_blake2b_ctx context;
    inventory_hash_begin(&context, key);
    uint8_t record[INVENTORY_RECORD_BYTES];
    for (size_t at = 0U; at < count &&
            result == OPENRFSFS_STATUS_OK; ++at) {
        result = encode_item(&items[at], record);
        if (result == OPENRFSFS_STATUS_OK)
            result = append_inventory_record(physical, key,
                &generation, record);
        if (result == OPENRFSFS_STATUS_OK)
            crypto_blake2b_update(&context, record, sizeof(record));
    }
    if (result == OPENRFSFS_STATUS_OK) {
        wipe(record, sizeof(record));
        copy_bytes(record, inventory_magic,
            sizeof(inventory_magic));
        record[4] = 3U;
        put_u64(record + 8U, count);
        crypto_blake2b_final(&context, record + 16U);
        result = append_inventory_record(physical, key,
            &generation, record);
    }
    wipe(record, sizeof(record));
    wipe(&context, sizeof(context));
    return result;
}

static enum openrfsfs_status source_matches(
    const struct vfs_backend_ops *physical,
    const uint8_t key[DATA_AEAD_KEY_BYTES],
    const struct inventory_item *expected, bool *present)
{
    *present = false;
    struct inventory_item observed = {0};
    enum openrfsfs_status result = source_stat(physical,
        expected->path, &observed.stat);
    if (result == OPENRFSFS_STATUS_NOT_FOUND)
        return OPENRFSFS_STATUS_OK;
    if (result != OPENRFSFS_STATUS_OK) return result;
    *present = true;
    observed.kind = observed.stat.directory ? DATA_NS_DIRECTORY :
        DATA_NS_FILE;
    observed.stat.mode &= 07777U;
    if (observed.kind == DATA_NS_DIRECTORY)
        observed.stat.size = 0U;
    copy_bytes(observed.path, expected->path,
        path_length(expected->path) + 1U);
    if (observed.kind == DATA_NS_FILE &&
            observed.stat.size == expected->stat.size)
        result = hash_source(physical, key, expected->path,
            expected->stat.size, observed.digest);
    if (result == OPENRFSFS_STATUS_OK) {
        const bool same = observed.kind == DATA_NS_DIRECTORY ?
            observed.stat.mode == expected->stat.mode &&
            observed.stat.attributes == expected->stat.attributes &&
            observed.stat.uid == expected->stat.uid &&
            observed.stat.gid == expected->stat.gid :
            same_item(&observed, expected);
        if (!same) result = OPENRFSFS_STATUS_CORRUPT;
    }
    wipe(&observed, sizeof(observed));
    return result;
}

static enum openrfsfs_status encrypted_matches(
    const struct vfs_backend_ops *physical,
    const uint8_t key[DATA_AEAD_KEY_BYTES],
    const struct inventory_item *item,
    const uint8_t expected_id[DATA_AEAD_ID_BYTES])
{
    char binding[DATA_AEAD_PATH_MAX + 1U];
    enum openrfsfs_status result = map_namespace(
        data_ns_file_binding(expected_id, binding));
    if (result != OPENRFSFS_STATUS_OK) return result;
    struct data_aead_backend_reader reader = {0};
    result = map_aead(data_aead_backend_reader_open(physical,
        OPENRFSFS_VOLUME_DATA, key, binding, workspace,
        sizeof(workspace), &reader));
    if (result == OPENRFSFS_STATUS_OK &&
            (crypto_verify16(reader.manifest.stable_id,
                expected_id) != 0 ||
             reader.manifest.plaintext_bytes != item->stat.size))
        result = OPENRFSFS_STATUS_CORRUPT;
    crypto_blake2b_ctx context;
    inventory_hash_begin(&context, key);
    uint8_t chunk[DATA_AEAD_CHUNK_BYTES];
    for (uint64_t offset = 0U; result == OPENRFSFS_STATUS_OK &&
            offset < item->stat.size;) {
        size_t amount = sizeof(chunk);
        if (item->stat.size - offset < amount)
            amount = (size_t)(item->stat.size - offset);
        size_t got = 0U;
        result = map_aead(data_aead_backend_reader_read(&reader,
            key, offset, chunk, amount, workspace,
            sizeof(workspace), &got));
        if (result == OPENRFSFS_STATUS_OK && got != amount)
            result = OPENRFSFS_STATUS_CORRUPT;
        if (result == OPENRFSFS_STATUS_OK)
            crypto_blake2b_update(&context, chunk, amount);
        offset += amount;
    }
    uint8_t digest[32];
    if (result == OPENRFSFS_STATUS_OK) {
        crypto_blake2b_final(&context, digest);
        if (crypto_verify32(digest, item->digest) != 0)
            result = OPENRFSFS_STATUS_CORRUPT;
    }
    if (reader.active &&
            data_aead_backend_reader_close(&reader) != DATA_AEAD_OK)
        result = OPENRFSFS_STATUS_IO;
    wipe(digest, sizeof(digest));
    wipe(chunk, sizeof(chunk));
    wipe(&context, sizeof(context));
    wipe(binding, sizeof(binding));
    return result;
}

static enum openrfsfs_status parent_id_for(
    const struct data_ns_state *state, const char *path,
    uint8_t parent_id[DATA_AEAD_ID_BYTES],
    char name[DATA_NS_NAME_BYTES + 1U])
{
    const size_t length = path_length(path);
    if (length == 0U || length > DATA_AEAD_PATH_MAX)
        return OPENRFSFS_STATUS_PATH;
    size_t slash = length;
    while (slash != 0U && path[slash - 1U] != '/') --slash;
    const size_t name_bytes = length - slash;
    if (name_bytes == 0U || name_bytes > DATA_NS_NAME_BYTES)
        return OPENRFSFS_STATUS_PATH;
    char parent[DATA_AEAD_PATH_MAX + 1U] = {0};
    if (slash == 0U) copy_bytes(parent, ".", 2U);
    else copy_bytes(parent, path, slash - 1U);
    copy_bytes(name, path + slash, name_bytes);
    enum data_ns_kind kind = 0;
    const enum openrfsfs_status result = map_namespace(
        data_ns_resolve(state, parent, parent_id, &kind));
    wipe(parent, sizeof(parent));
    if (result != OPENRFSFS_STATUS_OK) return result;
    return kind == DATA_NS_DIRECTORY ? OPENRFSFS_STATUS_OK :
        OPENRFSFS_STATUS_NOT_DIRECTORY;
}

static void event_metadata(const struct inventory_item *item,
    struct data_ns_event *event)
{
    event->mode = item->stat.mode & 07777U;
    event->attributes = item->stat.attributes;
    event->uid = item->stat.uid;
    event->gid = item->stat.gid;
    event->atime_seconds = (uint64_t)item->stat.atime_seconds;
    event->mtime_seconds = (uint64_t)item->stat.mtime_seconds;
    event->ctime_seconds = (uint64_t)item->stat.ctime_seconds;
    event->atime_nanos = item->stat.atime_nanos;
    event->mtime_nanos = item->stat.mtime_nanos;
    event->ctime_nanos = item->stat.ctime_nanos;
}

static bool entry_matches_event(const struct data_ns_entry *entry,
    const struct data_ns_event *event)
{
    return same_text(entry->name, event->name) &&
        entry->kind == event->kind &&
        crypto_verify16(entry->child_id, event->child_id) == 0 &&
        entry->mode == event->mode &&
        entry->attributes == event->attributes &&
        entry->uid == event->uid && entry->gid == event->gid &&
        entry->atime_seconds == event->atime_seconds &&
        entry->mtime_seconds == event->mtime_seconds &&
        entry->ctime_seconds == event->ctime_seconds &&
        entry->atime_nanos == event->atime_nanos &&
        entry->mtime_nanos == event->mtime_nanos &&
        entry->ctime_nanos == event->ctime_nanos;
}

static enum openrfsfs_status process_item(
    const struct vfs_backend_ops *physical,
    const uint8_t key[DATA_AEAD_KEY_BYTES],
    struct data_ns_state *state, uint64_t *generation,
    const struct inventory_item *item)
{
    struct data_ns_event event;
    wipe(&event, sizeof(event));
    event.kind = item->kind;
    event.operation = DATA_NS_CREATE;
    enum openrfsfs_status result = parent_id_for(state, item->path,
        event.parent_id, event.name);
    uint8_t unused_id[DATA_AEAD_ID_BYTES];
    if (result == OPENRFSFS_STATUS_OK)
        result = map_aead(data_aead_migration_ids(key,
            item->path, 0U, event.child_id, unused_id));
    wipe(unused_id, sizeof(unused_id));
    if (result != OPENRFSFS_STATUS_OK) goto done;
    event_metadata(item, &event);
    const struct data_ns_entry *present_entry = data_ns_find(state,
        event.parent_id, event.name);
    if (present_entry != NULL &&
            !entry_matches_event(present_entry, &event)) {
        result = OPENRFSFS_STATUS_CORRUPT;
        goto done;
    }
    bool source_present = false;
    result = source_matches(physical, key, item, &source_present);
    if (result != OPENRFSFS_STATUS_OK) goto done;
    if (!source_present && present_entry == NULL) {
        result = OPENRFSFS_STATUS_CORRUPT;
        goto done;
    }
    if (item->kind == DATA_NS_FILE && source_present) {
        char binding[DATA_AEAD_PATH_MAX + 1U];
        struct data_aead_manifest manifest = {0};
        result = map_namespace(data_ns_file_binding(event.child_id,
            binding));
        if (result == OPENRFSFS_STATUS_OK)
            result = map_aead(data_aead_backend_migrate_plain_as(
                physical, OPENRFSFS_VOLUME_DATA, key,
                item->path, binding, workspace,
                sizeof(workspace), &manifest));
        wipe(binding, sizeof(binding));
        wipe(&manifest, sizeof(manifest));
        if (result != OPENRFSFS_STATUS_OK) goto done;
    }
    if (item->kind == DATA_NS_FILE) {
        result = encrypted_matches(physical, key, item,
            event.child_id);
        if (result != OPENRFSFS_STATUS_OK) goto done;
    }
    if (present_entry == NULL) {
        uint64_t next_generation = 0U;
        result = map_namespace(data_ns_backend_append(physical,
            OPENRFSFS_VOLUME_DATA, key, workspace,
            sizeof(workspace), state, namespace_scratch,
            *generation, &event, &next_generation));
        if (result != OPENRFSFS_STATUS_OK) goto done;
        *generation = next_generation;
    }
    if (item->kind == DATA_NS_FILE && source_present) {
        result = physical->unlink(OPENRFSFS_VOLUME_DATA,
            item->path);
        if (result == OPENRFSFS_STATUS_OK)
            result = physical->sync(OPENRFSFS_VOLUME_DATA);
    }
done:
    wipe(&event, sizeof(event));
    return result;
}

static enum openrfsfs_status load_namespace(
    const struct vfs_backend_ops *physical,
    const uint8_t key[DATA_AEAD_KEY_BYTES],
    struct data_ns_state *state, uint64_t *generation,
    bool inventory_complete)
{
    state->entries = namespace_entries;
    state->capacity = INVENTORY_ITEMS;
    state->count = 0U;
    state->case_sensitive = physical->case_sensitive;
    enum openrfsfs_status result = map_namespace(
        data_ns_backend_load(physical, OPENRFSFS_VOLUME_DATA,
            key, workspace, sizeof(workspace), state,
            generation));
    if (result == OPENRFSFS_STATUS_NOT_FOUND &&
            !inventory_complete) {
        result = map_namespace(data_ns_backend_create_empty(
            physical, OPENRFSFS_VOLUME_DATA, key, workspace,
            sizeof(workspace), generation));
        if (result == OPENRFSFS_STATUS_OK)
            result = map_namespace(data_ns_backend_load(physical,
                OPENRFSFS_VOLUME_DATA, key, workspace,
                sizeof(workspace), state, generation));
    }
    return result == OPENRFSFS_STATUS_NOT_FOUND ?
        OPENRFSFS_STATUS_CORRUPT : result;
}

static enum openrfsfs_status final_physical_census(
    const struct vfs_backend_ops *physical, const char root[9])
{
    size_t count = 0U;
    enum openrfsfs_status result = physical->list(
        OPENRFSFS_VOLUME_DATA, ".", listed,
        INVENTORY_LIST_ENTRIES, &count);
    if (result != OPENRFSFS_STATUS_OK) return result;
    bool root_found = false;
    for (size_t at = 0U; at < count; ++at) {
        if (same_physical_name(physical, listed[at].name, root)) {
            if (!listed[at].directory || root_found)
                result = OPENRFSFS_STATUS_CORRUPT;
            root_found = true;
        } else if (same_physical_name(physical, listed[at].name,
            "OPENRFS")) {
            if (!listed[at].directory)
                result = OPENRFSFS_STATUS_CORRUPT;
        } else result = OPENRFSFS_STATUS_CORRUPT;
    }
    wipe(listed, sizeof(listed));
    if (result == OPENRFSFS_STATUS_OK && !root_found)
        result = OPENRFSFS_STATUS_CORRUPT;
    if (result == OPENRFSFS_STATUS_OK)
        result = credential_directory(physical);
    return result;
}

enum openrfsfs_status data_encrypted_migration_run(
    const struct vfs_backend_ops *physical,
    const uint8_t key[DATA_AEAD_KEY_BYTES])
{
    if (physical == NULL || key == NULL ||
            physical->list == NULL || physical->stat_path == NULL ||
            physical->open == NULL || physical->close == NULL ||
            physical->pread == NULL || physical->write == NULL ||
            physical->mkdir == NULL || physical->rename == NULL ||
            physical->unlink == NULL || physical->rmdir == NULL ||
            physical->sync == NULL)
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    enum openrfsfs_status result;
    char root[9];
    result = storage_root(key, root);
    if (result != OPENRFSFS_STATUS_OK) return result;
    size_t count = 0U;
    size_t partial = 0U;
    bool complete = false;
    result = load_inventory(physical, key, &count, &partial,
        &complete);
    if (result != OPENRFSFS_STATUS_OK &&
            result != OPENRFSFS_STATUS_NOT_FOUND) goto done;
    struct data_ns_state state = {0};
    uint64_t namespace_generation = 0U;
    result = load_namespace(physical, key, &state,
        &namespace_generation, complete);
    if (result != OPENRFSFS_STATUS_OK) goto done;
    if (!complete) {
        if (state.count != 0U) {
            result = OPENRFSFS_STATUS_CORRUPT;
            goto done;
        }
        for (size_t at = 0U; at < partial; ++at)
            prior_items[at] = items[at];
        result = scan_plain_tree(physical, key, root, &count);
        if (result != OPENRFSFS_STATUS_OK) goto done;
        if (partial > count) {
            result = OPENRFSFS_STATUS_CORRUPT;
            goto done;
        }
        for (size_t at = 0U; at < partial; ++at)
            if (!same_item(&prior_items[at], &items[at])) {
                result = OPENRFSFS_STATUS_CORRUPT;
                goto done;
            }
        result = write_inventory(physical, key, count);
        if (result != OPENRFSFS_STATUS_OK) goto done;
        size_t sealed_count = 0U;
        result = load_inventory(physical, key, &sealed_count,
            &partial, &complete);
        if (result != OPENRFSFS_STATUS_OK || !complete ||
                sealed_count != count) {
            result = OPENRFSFS_STATUS_CORRUPT;
            goto done;
        }
    }
    for (size_t at = 0U; at < count; ++at) {
        result = process_item(physical, key, &state,
            &namespace_generation, &items[at]);
        if (result != OPENRFSFS_STATUS_OK) goto done;
    }
    if (state.count != count) {
        result = OPENRFSFS_STATUS_CORRUPT;
        goto done;
    }
    for (size_t at = count; at != 0U; --at) {
        const struct inventory_item *item = &items[at - 1U];
        if (item->kind != DATA_NS_DIRECTORY) continue;
        result = physical->rmdir(OPENRFSFS_VOLUME_DATA,
            item->path);
        if (result == OPENRFSFS_STATUS_NOT_FOUND) {
            result = OPENRFSFS_STATUS_OK;
            continue;
        }
        if (result != OPENRFSFS_STATUS_OK) goto done;
        result = physical->sync(OPENRFSFS_VOLUME_DATA);
        if (result != OPENRFSFS_STATUS_OK) goto done;
    }
    result = final_physical_census(physical, root);
done:
    wipe(root, sizeof(root));
    wipe(items, sizeof(items));
    wipe(prior_items, sizeof(prior_items));
    wipe(listed, sizeof(listed));
    wipe(namespace_entries, sizeof(namespace_entries));
    wipe(namespace_scratch, sizeof(namespace_scratch));
    wipe(workspace, sizeof(workspace));
    return result;
}
