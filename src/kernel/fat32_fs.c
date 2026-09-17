/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * OpenRFS's bounded FAT32 file store. Raw metadata is accepted only after the
 * Rust parsers copy it into pointer-free values. Media traffic is synchronous
 * ordinary NVMe I/O; the small write-back cache has explicit mount ownership.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/console.h>
#include <openrfs/fat32_backend.h>
#include <openrfs/fat32_fs.h>
#include <openrfs/nvme.h>

#define OPENRFSFS_SECTOR_BYTES 512U
#define OPENRFSFS_DIRECTORY_ENTRIES (OPENRFSFS_SECTOR_BYTES / FAT32_DIRECTORY_ENTRY_BYTES)
#define OPENRFSFS_ATTR_READ_ONLY UINT8_C(0x01)
#define OPENRFSFS_ATTR_VOLUME UINT8_C(0x08)
#define OPENRFSFS_ATTR_DIRECTORY UINT8_C(0x10)
#define OPENRFSFS_ATTR_LONG_NAME UINT8_C(0x0F)
#define OPENRFSFS_MAX_CHAIN_CLUSTERS (OPENRFSFS_MAX_FILE_BYTES / OPENRFSFS_SECTOR_BYTES)
#define OPENRFSFS_SYSTEM_CONTROLLER 0U
#define OPENRFSFS_DATA_CONTROLLER 1U
#define OPENRFSFS_FSINFO_FREE_OFFSET 488U
#define OPENRFSFS_FSINFO_NEXT_OFFSET 492U
#define OPENRFSFS_FSINFO_TRAIL_OFFSET 508U
#define OPENRFSFS_VOLUME_SECTORS UINT64_C(131072)
#define OPENRFSFS_RESERVED_SECTORS 32U
#define OPENRFSFS_FAT_SECTORS UINT64_C(1009)
#define OPENRFSFS_ROOT_CLUSTER 2U
#define OPENRFSFS_MAX_MEDIA_CLUSTER 129023U
#define OPENRFSFS_VALIDATION_BITMAP_BYTES 16128U
#define OPENRFSFS_MAX_VALIDATION_DIRECTORIES 256U

struct openrfsfs_mount_state {
    struct fat32_geometry geometry;
    struct fat32_fsinfo fsinfo;
    uint64_t generation;
    uint64_t free_clusters;
    uint64_t completion_count;
    uint32_t next_free;
    uint32_t controller_index;
    bool active;
    bool present;
    bool healthy;
    bool read_only;
};

struct openrfsfs_handle_state {
    uint64_t generation;
    uint64_t mount_generation;
    uint64_t entry_sector;
    uint32_t entry_offset;
    uint32_t first_cluster;
    uint32_t size;
    uint32_t offset;
    enum openrfsfs_volume volume;
    enum openrfsfs_access access;
    bool active;
};

struct openrfsfs_cache_entry {
    uint8_t data[OPENRFSFS_SECTOR_BYTES];
    uint64_t mount_generation;
    uint64_t sector;
    uint64_t stamp;
    enum openrfsfs_volume volume;
    bool valid;
    bool dirty;
};

struct openrfsfs_operation {
    struct nvme_volume_session nvme;
    struct openrfsfs_mount_state *mount;
    enum openrfsfs_volume volume;
    bool active;
    bool writable;
};

struct openrfsfs_validation_directory {
    uint32_t cluster;
    uint32_t parent;
    uint32_t depth;
};

struct openrfsfs_location {
    struct fat32_directory_entry entry;
    uint64_t sector;
    uint32_t offset;
    uint32_t parent_cluster;
    bool root;
    bool found;
};

struct openrfsfs_parent {
    uint32_t cluster;
    struct fat32_name name;
};

static struct openrfsfs_mount_state mounts[OPENRFSFS_MAX_MOUNTS];
static struct openrfsfs_handle_state handles[OPENRFSFS_MAX_HANDLES];
static struct openrfsfs_cache_entry cache[OPENRFSFS_CACHE_ENTRIES];
static uint8_t validation_seen[OPENRFSFS_VALIDATION_BITMAP_BYTES];
static struct openrfsfs_validation_directory
    validation_directories[OPENRFSFS_MAX_VALIDATION_DIRECTORIES];
static uint64_t next_mount_generation = UINT64_C(1);
static uint64_t next_handle_generation = UINT64_C(1);
static uint64_t cache_stamp;

_Static_assert(sizeof(struct fat32_geometry) == 96U,
    "Rust/C FAT32 geometry ABI changed");
_Static_assert(sizeof(struct fat32_fsinfo) == 16U,
    "Rust/C FAT32 FSInfo ABI changed");
_Static_assert(sizeof(struct fat32_directory_entry) == 28U,
    "Rust/C FAT32 directory ABI changed");
_Static_assert(sizeof(struct fat32_name) == 16U,
    "Rust/C FAT32 name ABI changed");
_Static_assert(FAT32_STATUS_COUNT == 37,
    "Rust/C FAT32 status ABI changed");
_Static_assert(OPENRFSFS_MAX_FILE_BYTES % OPENRFSFS_SECTOR_BYTES == 0U,
    "file bound must contain whole clusters");
_Static_assert(OPENRFSFS_MAX_HANDLES <= UINT8_MAX,
    "file handle index no longer fits its encoded byte");

static void zero_bytes(void *pointer, size_t length)
{
    uint8_t *bytes = pointer;

    for (size_t index = 0U; index < length; ++index) {
        bytes[index] = 0U;
    }
}

static void copy_bytes(uint8_t *destination, const uint8_t *source, size_t length)
{
    for (size_t index = 0U; index < length; ++index) {
        destination[index] = source[index];
    }
}

static bool equal_bytes(const uint8_t *left, const uint8_t *right, size_t length)
{
    for (size_t index = 0U; index < length; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8U |
        (uint32_t)bytes[2] << 16U | (uint32_t)bytes[3] << 24U;
}

static void write_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
}

static void write_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static bool add_u64(uint64_t left, uint64_t right, uint64_t *sum)
{
    if (sum == NULL || left > UINT64_MAX - right) {
        return false;
    }
    *sum = left + right;
    return true;
}

static bool multiply_u64(uint64_t left, uint64_t right, uint64_t *product)
{
    if (product == NULL || (left != 0U && right > UINT64_MAX / left)) {
        return false;
    }
    *product = left * right;
    return true;
}

static size_t bounded_length(const char *text, size_t limit)
{
    size_t length = 0U;

    if (text == NULL) {
        return limit + 1U;
    }
    while (length <= limit && text[length] != '\0') {
        ++length;
    }
    return length;
}

static bool valid_volume(enum openrfsfs_volume volume)
{
    return volume >= OPENRFSFS_VOLUME_SYSTEM && volume < OPENRFSFS_VOLUME_COUNT;
}

static enum openrfsfs_status nvme_result(enum nvme_status status)
{
    if (status == NVME_STATUS_OK) {
        return OPENRFSFS_STATUS_OK;
    }
    if (status == NVME_STATUS_ABSENT ||
        status == NVME_STATUS_NAMESPACE_ABSENT ||
        status == NVME_STATUS_NAMESPACE_INACTIVE) {
        return OPENRFSFS_STATUS_ABSENT;
    }
    if (status == NVME_STATUS_COMPLETION_STATUS ||
        status == NVME_STATUS_WRITE_VERIFY) {
        return OPENRFSFS_STATUS_WRITEBACK;
    }
    if (status == NVME_STATUS_VOLUME_READ_ONLY) {
        return OPENRFSFS_STATUS_READ_ONLY;
    }
    if (status == NVME_STATUS_SESSION_INVALID ||
        status == NVME_STATUS_TEARDOWN_RACE) {
        return OPENRFSFS_STATUS_RESET;
    }
    return OPENRFSFS_STATUS_IO;
}

static void report_nvme_failure(const char *operation, enum nvme_status status)
{
    console_write("OpenRFS: FAT32 NVMe ");
    console_write(operation);
    console_write(" failed: ");
    console_write(nvme_status_string(status));
    console_write("\n");
}

static void invalidate_cache(enum openrfsfs_volume volume)
{
    for (size_t index = 0U; index < OPENRFSFS_CACHE_ENTRIES; ++index) {
        if (cache[index].valid && cache[index].volume == volume) {
            zero_bytes(&cache[index], sizeof(cache[index]));
        }
    }
}

static enum openrfsfs_status direct_read(
    struct nvme_volume_session *session,
    uint64_t sector,
    uint8_t *data
)
{
    enum nvme_status status = nvme_volume_read(session, sector, data,
        OPENRFSFS_SECTOR_BYTES);

    if (status != NVME_STATUS_OK) {
        report_nvme_failure("read", status);
    }
    return nvme_result(status);
}

static enum openrfsfs_status direct_write(
    struct nvme_volume_session *session,
    uint64_t sector,
    const uint8_t *data
)
{
    enum nvme_status status = nvme_volume_write(session, sector, data,
        OPENRFSFS_SECTOR_BYTES);

    if (status != NVME_STATUS_OK) {
        report_nvme_failure("write", status);
    }
    return nvme_result(status);
}

static enum openrfsfs_status flush_cache_entry(
    struct openrfsfs_operation *operation,
    struct openrfsfs_cache_entry *entry
)
{
    enum openrfsfs_status status;

    if (operation == NULL || entry == NULL || !operation->active) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    if (!entry->valid || !entry->dirty) {
        return OPENRFSFS_STATUS_OK;
    }
    if (!operation->writable || entry->volume != operation->volume ||
        entry->mount_generation != operation->mount->generation) {
        return OPENRFSFS_STATUS_READ_ONLY;
    }
    status = direct_write(&operation->nvme, entry->sector, entry->data);
    if (status == OPENRFSFS_STATUS_OK) {
        entry->dirty = false;
    }
    return status;
}

static enum openrfsfs_status flush_cache(struct openrfsfs_operation *operation)
{
    for (size_t index = 0U; index < OPENRFSFS_CACHE_ENTRIES; ++index) {
        enum openrfsfs_status status = flush_cache_entry(operation, &cache[index]);

        if (status != OPENRFSFS_STATUS_OK) {
            return status;
        }
    }
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status cache_sector(
    struct openrfsfs_operation *operation,
    uint64_t sector,
    struct openrfsfs_cache_entry **result
)
{
    struct openrfsfs_cache_entry *selected = NULL;

    if (operation == NULL || result == NULL || !operation->active ||
        sector >= operation->mount->geometry.total_sectors) {
        return OPENRFSFS_STATUS_RANGE;
    }
    *result = NULL;
    for (size_t index = 0U; index < OPENRFSFS_CACHE_ENTRIES; ++index) {
        struct openrfsfs_cache_entry *entry = &cache[index];

        if (entry->valid && entry->volume == operation->volume &&
            entry->mount_generation == operation->mount->generation &&
            entry->sector == sector) {
            entry->stamp = ++cache_stamp;
            *result = entry;
            return OPENRFSFS_STATUS_OK;
        }
        if (!entry->valid || selected == NULL ||
            entry->stamp < selected->stamp) {
            selected = entry;
        }
    }
    if (selected == NULL) {
        return OPENRFSFS_STATUS_IO;
    }
    if (selected->valid && selected->dirty) {
        enum openrfsfs_status status = flush_cache_entry(operation, selected);

        if (status != OPENRFSFS_STATUS_OK) {
            return status;
        }
    }
    zero_bytes(selected, sizeof(*selected));
    if (direct_read(&operation->nvme, sector, selected->data) !=
            OPENRFSFS_STATUS_OK) {
        return OPENRFSFS_STATUS_IO;
    }
    selected->mount_generation = operation->mount->generation;
    selected->sector = sector;
    selected->stamp = ++cache_stamp;
    selected->volume = operation->volume;
    selected->valid = true;
    *result = selected;
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status flush_one(
    struct openrfsfs_operation *operation,
    struct openrfsfs_cache_entry *entry
)
{
    if (entry == NULL) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    entry->dirty = true;
    return flush_cache_entry(operation, entry);
}

static enum openrfsfs_status begin_operation(
    enum openrfsfs_volume volume,
    bool writable,
    struct openrfsfs_operation *operation
)
{
    struct openrfsfs_mount_state *mount;
    enum nvme_status status;

    if (!valid_volume(volume) || operation == NULL) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    mount = &mounts[volume];
    if (!mount->active || !mount->healthy) {
        return OPENRFSFS_STATUS_NOT_MOUNTED;
    }
    if (writable && mount->read_only) {
        return OPENRFSFS_STATUS_READ_ONLY;
    }
    zero_bytes(operation, sizeof(*operation));
    status = nvme_volume_open(&operation->nvme, mount->controller_index,
        writable);
    if (status != NVME_STATUS_OK) {
        report_nvme_failure("open", status);
        invalidate_cache(volume);
        return nvme_result(status);
    }
    if (operation->nvme.namespace_blocks != mount->geometry.total_sectors ||
        operation->nvme.logical_block_bytes !=
            mount->geometry.bytes_per_sector) {
        (void)nvme_volume_close(&operation->nvme);
        invalidate_cache(volume);
        return OPENRFSFS_STATUS_RESET;
    }
    operation->mount = mount;
    operation->volume = volume;
    operation->active = true;
    operation->writable = writable;
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status end_operation(
    struct openrfsfs_operation *operation,
    bool commit
)
{
    enum openrfsfs_status result = OPENRFSFS_STATUS_OK;
    enum nvme_status close_status;
    enum nvme_status flush_status;
    uint64_t total;

    if (operation == NULL || !operation->active) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    if (commit) {
        result = flush_cache(operation);
        if (result == OPENRFSFS_STATUS_OK) {
            flush_status = nvme_volume_flush(&operation->nvme);
            if (flush_status != NVME_STATUS_OK) {
                report_nvme_failure("flush", flush_status);
                result = nvme_result(flush_status);
            }
        }
    }
    if (add_u64(operation->mount->completion_count,
            operation->nvme.completion_count, &total)) {
        operation->mount->completion_count = total;
    } else {
        operation->mount->completion_count = UINT64_MAX;
    }
    invalidate_cache(operation->volume);
    close_status = nvme_volume_close(&operation->nvme);
    operation->active = false;
    if (result == OPENRFSFS_STATUS_OK && close_status != NVME_STATUS_OK) {
        report_nvme_failure("close", close_status);
        result = nvme_result(close_status);
    }
    return result;
}

static enum openrfsfs_status fat_entry(
    struct openrfsfs_operation *operation,
    uint32_t cluster,
    uint32_t *value
)
{
    struct openrfsfs_cache_entry *entry;
    uint64_t byte_offset;
    uint64_t sector;
    uint32_t offset;
    enum openrfsfs_status status;

    if (operation == NULL || value == NULL || cluster < 2U ||
        cluster > operation->mount->geometry.maximum_cluster ||
        !multiply_u64(cluster, 4U, &byte_offset)) {
        return OPENRFSFS_STATUS_RANGE;
    }
    sector = operation->mount->geometry.first_fat_sector +
        byte_offset / OPENRFSFS_SECTOR_BYTES;
    offset = (uint32_t)(byte_offset % OPENRFSFS_SECTOR_BYTES);
    status = cache_sector(operation, sector, &entry);
    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    *value = read_u32(&entry->data[offset]) & UINT32_C(0x0FFFFFFF);
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status set_fat_copy(
    struct openrfsfs_operation *operation,
    uint32_t copy,
    uint32_t cluster,
    uint32_t value
)
{
    struct openrfsfs_cache_entry *entry;
    uint64_t byte_offset;
    uint64_t sector;
    uint32_t offset;
    enum openrfsfs_status status;

    if (operation == NULL || !operation->writable || copy >= 2U ||
        cluster < 2U ||
        cluster > operation->mount->geometry.maximum_cluster ||
        !multiply_u64(cluster, 4U, &byte_offset)) {
        return OPENRFSFS_STATUS_RANGE;
    }
    sector = operation->mount->geometry.first_fat_sector +
        (uint64_t)copy * operation->mount->geometry.fat_sectors +
        byte_offset / OPENRFSFS_SECTOR_BYTES;
    offset = (uint32_t)(byte_offset % OPENRFSFS_SECTOR_BYTES);
    status = cache_sector(operation, sector, &entry);
    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    write_u32(&entry->data[offset], value & UINT32_C(0x0FFFFFFF));
    return flush_one(operation, entry);
}

static enum openrfsfs_status set_fat(
    struct openrfsfs_operation *operation,
    uint32_t cluster,
    uint32_t value
)
{
    enum openrfsfs_status status;

    /* Secondary first; the primary FAT is the commit copy. */
    status = set_fat_copy(operation, 1U, cluster, value);
    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    return set_fat_copy(operation, 0U, cluster, value);
}

static enum openrfsfs_status cluster_sector(
    const struct openrfsfs_mount_state *mount,
    uint32_t cluster,
    uint64_t *sector
)
{
    uint64_t relative;

    if (mount == NULL || sector == NULL || cluster < 2U ||
        cluster > mount->geometry.maximum_cluster ||
        !multiply_u64((uint64_t)cluster - 2U,
            mount->geometry.sectors_per_cluster, &relative) ||
        !add_u64(mount->geometry.first_data_sector, relative, sector) ||
        *sector >= mount->geometry.total_sectors) {
        return OPENRFSFS_STATUS_CORRUPT;
    }
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status next_cluster(
    struct openrfsfs_operation *operation,
    uint32_t cluster,
    uint32_t *next,
    bool *end
)
{
    uint32_t raw;
    uint32_t checked = 0U;
    enum openrfsfs_status status = fat_entry(operation, cluster, &raw);

    if (next == NULL || end == NULL || status != OPENRFSFS_STATUS_OK) {
        return status != OPENRFSFS_STATUS_OK ? status : OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    if (raw >= FAT32_EOC_MIN) {
        *next = raw;
        *end = true;
        return OPENRFSFS_STATUS_OK;
    }
    if (openrfs_fat32_classify_cluster(raw, &operation->mount->geometry,
            &checked) != FAT32_STATUS_OK) {
        return OPENRFSFS_STATUS_CORRUPT;
    }
    *next = checked;
    *end = false;
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status allocate_cluster(
    struct openrfsfs_operation *operation,
    uint32_t *allocated
)
{
    uint32_t start;
    uint32_t cluster = 0U;
    uint64_t attempts;

    if (operation == NULL || allocated == NULL || !operation->writable) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    if (operation->mount->free_clusters == 0U) {
        return OPENRFSFS_STATUS_FULL;
    }
    start = operation->mount->next_free;
    if (start < 2U || start > operation->mount->geometry.maximum_cluster) {
        start = 2U;
    }
    cluster = start;
    for (attempts = 0U;
         attempts < operation->mount->geometry.cluster_count; ++attempts) {
        uint32_t value;
        enum openrfsfs_status status = fat_entry(operation, cluster, &value);

        if (status != OPENRFSFS_STATUS_OK) {
            return status;
        }
        if (value == 0U) {
            uint64_t sector;
            struct openrfsfs_cache_entry *entry;

            status = cluster_sector(operation->mount, cluster, &sector);
            if (status != OPENRFSFS_STATUS_OK) {
                return status;
            }
            status = cache_sector(operation, sector, &entry);
            if (status != OPENRFSFS_STATUS_OK) {
                return status;
            }
            zero_bytes(entry->data, sizeof(entry->data));
            status = flush_one(operation, entry);
            if (status != OPENRFSFS_STATUS_OK) {
                return status;
            }
            status = set_fat(operation, cluster, FAT32_EOC);
            if (status != OPENRFSFS_STATUS_OK) {
                (void)set_fat(operation, cluster, 0U);
                return status;
            }
            --operation->mount->free_clusters;
            operation->mount->next_free = cluster ==
                operation->mount->geometry.maximum_cluster ? 2U : cluster + 1U;
            *allocated = cluster;
            return OPENRFSFS_STATUS_OK;
        }
        cluster = cluster == operation->mount->geometry.maximum_cluster ?
            2U : cluster + 1U;
    }
    return OPENRFSFS_STATUS_FULL;
}

static enum openrfsfs_status release_chain(
    struct openrfsfs_operation *operation,
    uint32_t first_cluster
)
{
    uint32_t cluster = first_cluster;

    if (first_cluster == 0U) {
        return OPENRFSFS_STATUS_OK;
    }
    for (uint32_t steps = 0U; steps < OPENRFSFS_MAX_CHAIN_CLUSTERS; ++steps) {
        uint32_t next;
        bool end;
        enum openrfsfs_status status = next_cluster(operation, cluster, &next, &end);

        if (status != OPENRFSFS_STATUS_OK) {
            return status;
        }
        status = set_fat(operation, cluster, 0U);
        if (status != OPENRFSFS_STATUS_OK) {
            return status;
        }
        ++operation->mount->free_clusters;
        if (cluster < operation->mount->next_free) {
            operation->mount->next_free = cluster;
        }
        if (end) {
            return OPENRFSFS_STATUS_OK;
        }
        cluster = next;
    }
    return OPENRFSFS_STATUS_CORRUPT;
}

struct openrfsfs_free_slot {
    uint64_t sector;
    size_t live_entries;
    uint32_t offset;
    uint32_t last_cluster;
    bool available;
};

static enum openrfsfs_status path_validation(const char *path, size_t *length)
{
    uint32_t components = 0U;
    size_t found = bounded_length(path, OPENRFSFS_MAX_PATH);
    enum fat32_status status;

    if (length == NULL || found == 0U || found > OPENRFSFS_MAX_PATH) {
        return found == 0U ? OPENRFSFS_STATUS_PATH : OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    status = openrfs_fat32_validate_path((const uint8_t *)path, found,
        &components);
    if (status == FAT32_STATUS_NAME_MALFORMED ||
        status == FAT32_STATUS_COMPONENT_TOO_LONG) {
        return OPENRFSFS_STATUS_NAME;
    }
    if (status != FAT32_STATUS_OK || components == 0U) {
        return OPENRFSFS_STATUS_PATH;
    }
    *length = found;
    return OPENRFSFS_STATUS_OK;
}

static bool name_equal(const uint8_t *left, const uint8_t *right)
{
    return equal_bytes(left, right, FAT32_SHORT_NAME_BYTES);
}

static enum openrfsfs_status scan_directory(
    struct openrfsfs_operation *operation,
    uint32_t directory_cluster,
    const struct fat32_name *query,
    struct openrfsfs_location *location,
    struct openrfsfs_free_slot *free_slot
)
{
    uint32_t cluster = directory_cluster;

    if (operation == NULL || directory_cluster < 2U) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    if (location != NULL) {
        zero_bytes(location, sizeof(*location));
    }
    if (free_slot != NULL) {
        zero_bytes(free_slot, sizeof(*free_slot));
    }
    for (uint32_t steps = 0U; steps < OPENRFSFS_MAX_CHAIN_CLUSTERS; ++steps) {
        uint64_t first_sector;
        uint32_t next;
        bool end;
        enum openrfsfs_status status = cluster_sector(operation->mount, cluster,
            &first_sector);

        if (status != OPENRFSFS_STATUS_OK) {
            return status;
        }
        if (free_slot != NULL) {
            free_slot->last_cluster = cluster;
        }
        for (uint32_t sector_index = 0U;
             sector_index < operation->mount->geometry.sectors_per_cluster;
             ++sector_index) {
            struct openrfsfs_cache_entry *sector;
            uint64_t sector_number = first_sector + sector_index;

            status = cache_sector(operation, sector_number, &sector);
            if (status != OPENRFSFS_STATUS_OK) {
                return status;
            }
            for (uint32_t offset = 0U; offset < OPENRFSFS_SECTOR_BYTES;
                 offset += FAT32_DIRECTORY_ENTRY_BYTES) {
                struct fat32_directory_entry parsed;
                enum fat32_status parse_status =
                    openrfs_fat32_parse_directory_entry(
                        &sector->data[offset], FAT32_DIRECTORY_ENTRY_BYTES,
                        &parsed);

                if (parse_status != FAT32_STATUS_OK) {
                    return OPENRFSFS_STATUS_CORRUPT;
                }
                if (parsed.kind == FAT32_ENTRY_END) {
                    if (free_slot != NULL && free_slot->live_entries >=
                            OPENRFSFS_MAX_LIST_ENTRIES) {
                        return OPENRFSFS_STATUS_DIRECTORY_FULL;
                    }
                    if (free_slot != NULL && !free_slot->available) {
                        free_slot->sector = sector_number;
                        free_slot->offset = offset;
                        free_slot->available = true;
                    }
                    return OPENRFSFS_STATUS_NOT_FOUND;
                }
                if (parsed.kind == FAT32_ENTRY_DELETED) {
                    if (free_slot != NULL && !free_slot->available) {
                        free_slot->sector = sector_number;
                        free_slot->offset = offset;
                        free_slot->available = true;
                    }
                    continue;
                }
                if (parsed.kind != FAT32_ENTRY_ORDINARY ||
                    (parsed.attributes & OPENRFSFS_ATTR_VOLUME) != 0U) {
                    continue;
                }
                if (free_slot != NULL &&
                    !name_equal(parsed.short_name,
                        (const uint8_t *)".          ") &&
                    !name_equal(parsed.short_name,
                        (const uint8_t *)"..         ")) {
                    ++free_slot->live_entries;
                    if (free_slot->live_entries > OPENRFSFS_MAX_LIST_ENTRIES) {
                        return OPENRFSFS_STATUS_CORRUPT;
                    }
                }
                if (query != NULL && query->kind == FAT32_NAME_ORDINARY &&
                    name_equal(parsed.short_name, query->canonical)) {
                    if (location == NULL) {
                        return OPENRFSFS_STATUS_OK;
                    }
                    location->entry = parsed;
                    location->sector = sector_number;
                    location->offset = offset;
                    location->parent_cluster = directory_cluster;
                    location->found = true;
                    return OPENRFSFS_STATUS_OK;
                }
            }
        }
        status = next_cluster(operation, cluster, &next, &end);
        if (status != OPENRFSFS_STATUS_OK) {
            return status;
        }
        if (end) {
            if (free_slot != NULL && free_slot->live_entries >=
                    OPENRFSFS_MAX_LIST_ENTRIES) {
                return OPENRFSFS_STATUS_DIRECTORY_FULL;
            }
            return OPENRFSFS_STATUS_NOT_FOUND;
        }
        cluster = next;
    }
    return OPENRFSFS_STATUS_CORRUPT;
}

static enum openrfsfs_status resolve_path(
    struct openrfsfs_operation *operation,
    const char *path,
    struct openrfsfs_location *location
)
{
    uint32_t stack[OPENRFSFS_MAX_DEPTH + 1U];
    size_t length;
    size_t start = 0U;
    uint32_t depth = 0U;
    enum openrfsfs_status status = path_validation(path, &length);

    if (status != OPENRFSFS_STATUS_OK || operation == NULL || location == NULL) {
        return status != OPENRFSFS_STATUS_OK ? status : OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    stack[0] = operation->mount->geometry.root_cluster;
    zero_bytes(location, sizeof(*location));
    while (start < length) {
        size_t end = start;
        struct fat32_name name;
        bool last;

        while (end < length && path[end] != '/') {
            ++end;
        }
        last = end == length;
        if (openrfs_fat32_parse_component((const uint8_t *)&path[start],
                end - start, &name) != FAT32_STATUS_OK) {
            return OPENRFSFS_STATUS_NAME;
        }
        if (name.kind == FAT32_NAME_CURRENT) {
            if (last) {
                location->entry.attributes = OPENRFSFS_ATTR_DIRECTORY;
                location->entry.first_cluster = stack[depth];
                location->entry.valid = 1U;
                location->root = depth == 0U;
                location->found = true;
                return OPENRFSFS_STATUS_OK;
            }
        } else if (name.kind == FAT32_NAME_PARENT) {
            if (depth == 0U) {
                return OPENRFSFS_STATUS_PATH;
            }
            --depth;
            if (last) {
                location->entry.attributes = OPENRFSFS_ATTR_DIRECTORY;
                location->entry.first_cluster = stack[depth];
                location->entry.valid = 1U;
                location->root = depth == 0U;
                location->found = true;
                return OPENRFSFS_STATUS_OK;
            }
        } else {
            status = scan_directory(operation, stack[depth], &name, location,
                NULL);
            if (status != OPENRFSFS_STATUS_OK) {
                return status;
            }
            if (last) {
                return OPENRFSFS_STATUS_OK;
            }
            if ((location->entry.attributes & OPENRFSFS_ATTR_DIRECTORY) == 0U) {
                return OPENRFSFS_STATUS_NOT_DIRECTORY;
            }
            if (depth >= OPENRFSFS_MAX_DEPTH) {
                return OPENRFSFS_STATUS_PATH;
            }
            ++depth;
            stack[depth] = location->entry.first_cluster;
        }
        start = end + 1U;
    }
    return OPENRFSFS_STATUS_NOT_FOUND;
}

static enum openrfsfs_status resolve_parent(
    struct openrfsfs_operation *operation,
    const char *path,
    struct openrfsfs_parent *parent
)
{
    char parent_path[OPENRFSFS_MAX_PATH + 1U];
    size_t length;
    size_t split;
    struct openrfsfs_location location;
    enum openrfsfs_status status = path_validation(path, &length);

    if (status != OPENRFSFS_STATUS_OK || operation == NULL || parent == NULL) {
        return status != OPENRFSFS_STATUS_OK ? status : OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    split = length;
    while (split != 0U && path[split - 1U] != '/') {
        --split;
    }
    if (openrfs_fat32_parse_component((const uint8_t *)&path[split],
            length - split, &parent->name) != FAT32_STATUS_OK ||
        parent->name.kind != FAT32_NAME_ORDINARY) {
        return OPENRFSFS_STATUS_NAME;
    }
    if (split == 0U) {
        parent->cluster = operation->mount->geometry.root_cluster;
        return OPENRFSFS_STATUS_OK;
    }
    copy_bytes((uint8_t *)parent_path, (const uint8_t *)path, split - 1U);
    parent_path[split - 1U] = '\0';
    status = resolve_path(operation, parent_path, &location);
    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    if ((location.entry.attributes & OPENRFSFS_ATTR_DIRECTORY) == 0U) {
        return OPENRFSFS_STATUS_NOT_DIRECTORY;
    }
    parent->cluster = location.entry.first_cluster;
    return OPENRFSFS_STATUS_OK;
}

static void encode_directory_entry(
    uint8_t *bytes,
    const uint8_t *name,
    uint8_t attributes,
    uint32_t first_cluster,
    uint32_t size
)
{
    zero_bytes(bytes, FAT32_DIRECTORY_ENTRY_BYTES);
    copy_bytes(bytes, name, FAT32_SHORT_NAME_BYTES);
    bytes[11] = attributes;
    write_u16(&bytes[20], (uint16_t)(first_cluster >> 16U));
    write_u16(&bytes[26], (uint16_t)first_cluster);
    write_u32(&bytes[28], size);
}

static enum openrfsfs_status write_directory_entry(
    struct openrfsfs_operation *operation,
    uint64_t sector_number,
    uint32_t offset,
    const uint8_t *name,
    uint8_t attributes,
    uint32_t first_cluster,
    uint32_t size
)
{
    struct openrfsfs_cache_entry *sector;
    enum openrfsfs_status status;

    if (offset > OPENRFSFS_SECTOR_BYTES - FAT32_DIRECTORY_ENTRY_BYTES) {
        return OPENRFSFS_STATUS_RANGE;
    }
    status = cache_sector(operation, sector_number, &sector);
    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    encode_directory_entry(&sector->data[offset], name, attributes,
        first_cluster, size);
    return flush_one(operation, sector);
}

static enum openrfsfs_status update_location(
    struct openrfsfs_operation *operation,
    const struct openrfsfs_location *location,
    uint32_t first_cluster,
    uint32_t size
)
{
    if (location == NULL || !location->found || location->root) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    return write_directory_entry(operation, location->sector,
        location->offset, location->entry.short_name,
        location->entry.attributes, first_cluster, size);
}

static enum openrfsfs_status mark_deleted(
    struct openrfsfs_operation *operation,
    const struct openrfsfs_location *location
)
{
    struct openrfsfs_cache_entry *sector;
    enum openrfsfs_status status;

    if (location == NULL || !location->found || location->root) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    status = cache_sector(operation, location->sector, &sector);
    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    sector->data[location->offset] = UINT8_C(0xE5);
    return flush_one(operation, sector);
}

static enum openrfsfs_status find_or_grow_slot(
    struct openrfsfs_operation *operation,
    uint32_t directory_cluster,
    const struct fat32_name *name,
    struct openrfsfs_free_slot *slot
)
{
    struct openrfsfs_location duplicate;
    enum openrfsfs_status status = scan_directory(operation, directory_cluster,
        name, &duplicate, slot);

    if (status == OPENRFSFS_STATUS_OK) {
        return OPENRFSFS_STATUS_EXISTS;
    }
    if (status != OPENRFSFS_STATUS_NOT_FOUND) {
        return status;
    }
    if (slot->available) {
        return OPENRFSFS_STATUS_OK;
    }
    uint32_t allocated;
    status = allocate_cluster(operation, &allocated);
    if (status != OPENRFSFS_STATUS_OK) {
        return status == OPENRFSFS_STATUS_FULL ? OPENRFSFS_STATUS_DIRECTORY_FULL : status;
    }
    status = set_fat(operation, slot->last_cluster, allocated);
    if (status != OPENRFSFS_STATUS_OK) {
        (void)release_chain(operation, allocated);
        return status;
    }
    status = cluster_sector(operation->mount, allocated, &slot->sector);
    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    slot->offset = 0U;
    slot->last_cluster = allocated;
    slot->available = true;
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status chain_cluster_at(
    struct openrfsfs_operation *operation,
    uint32_t first_cluster,
    uint32_t ordinal,
    uint32_t *result
)
{
    uint32_t cluster = first_cluster;

    if (result == NULL || first_cluster < 2U ||
        ordinal >= OPENRFSFS_MAX_CHAIN_CLUSTERS) {
        return OPENRFSFS_STATUS_RANGE;
    }
    for (uint32_t index = 0U; index < ordinal; ++index) {
        uint32_t next;
        bool end;
        enum openrfsfs_status status = next_cluster(operation, cluster, &next, &end);

        if (status != OPENRFSFS_STATUS_OK || end) {
            return status != OPENRFSFS_STATUS_OK ? status : OPENRFSFS_STATUS_CORRUPT;
        }
        cluster = next;
    }
    *result = cluster;
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status zero_chain_bytes(
    struct openrfsfs_operation *operation,
    uint32_t first_cluster,
    uint32_t start,
    uint32_t end
)
{
    while (start < end) {
        uint32_t ordinal = start / OPENRFSFS_SECTOR_BYTES;
        uint32_t within = start % OPENRFSFS_SECTOR_BYTES;
        uint32_t cluster;
        uint64_t sector_number;
        struct openrfsfs_cache_entry *sector;
        uint32_t chunk = OPENRFSFS_SECTOR_BYTES - within;
        enum openrfsfs_status status;

        if (chunk > end - start) {
            chunk = end - start;
        }
        status = chain_cluster_at(operation, first_cluster, ordinal, &cluster);
        if (status == OPENRFSFS_STATUS_OK) {
            status = cluster_sector(operation->mount, cluster, &sector_number);
        }
        if (status == OPENRFSFS_STATUS_OK) {
            status = cache_sector(operation, sector_number, &sector);
        }
        if (status != OPENRFSFS_STATUS_OK) {
            return status;
        }
        zero_bytes(&sector->data[within], chunk);
        status = flush_one(operation, sector);
        if (status != OPENRFSFS_STATUS_OK) {
            return status;
        }
        start += chunk;
    }
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status count_chain(
    struct openrfsfs_operation *operation,
    uint32_t first_cluster,
    uint32_t *count,
    uint32_t *last
)
{
    uint32_t cluster = first_cluster;

    if (count == NULL || last == NULL) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    *count = 0U;
    *last = 0U;
    if (first_cluster == 0U) {
        return OPENRFSFS_STATUS_OK;
    }
    for (uint32_t steps = 0U; steps < OPENRFSFS_MAX_CHAIN_CLUSTERS; ++steps) {
        uint32_t next;
        bool end;
        enum openrfsfs_status status;

        ++*count;
        *last = cluster;
        status = next_cluster(operation, cluster, &next, &end);
        if (status != OPENRFSFS_STATUS_OK) {
            return status;
        }
        if (end) {
            return OPENRFSFS_STATUS_OK;
        }
        cluster = next;
    }
    return OPENRFSFS_STATUS_CORRUPT;
}

static enum openrfsfs_status ensure_chain(
    struct openrfsfs_operation *operation,
    uint32_t existing_first,
    uint32_t required,
    uint32_t *result_first
)
{
    uint32_t count;
    uint32_t existing_last;
    uint32_t first_new = 0U;
    uint32_t last_new = 0U;
    enum openrfsfs_status status;

    if (required > OPENRFSFS_MAX_CHAIN_CLUSTERS || result_first == NULL) {
        return OPENRFSFS_STATUS_RANGE;
    }
    status = count_chain(operation, existing_first, &count, &existing_last);
    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    if (count >= required) {
        *result_first = existing_first;
        return OPENRFSFS_STATUS_OK;
    }
    while (count < required) {
        uint32_t allocated;

        status = allocate_cluster(operation, &allocated);
        if (status != OPENRFSFS_STATUS_OK) {
            if (first_new != 0U) {
                (void)release_chain(operation, first_new);
            }
            return status;
        }
        if (first_new == 0U) {
            first_new = allocated;
        } else {
            status = set_fat(operation, last_new, allocated);
            if (status != OPENRFSFS_STATUS_OK) {
                (void)release_chain(operation, allocated);
                (void)release_chain(operation, first_new);
                return status;
            }
        }
        last_new = allocated;
        ++count;
    }
    if (existing_first != 0U) {
        status = set_fat(operation, existing_last, first_new);
        if (status != OPENRFSFS_STATUS_OK) {
            (void)release_chain(operation, first_new);
            return status;
        }
        *result_first = existing_first;
    } else {
        *result_first = first_new;
    }
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status validate_volume_identity(
    enum openrfsfs_volume volume,
    const struct fat32_geometry *geometry
)
{
    static const uint8_t system_label[11] =
        {'O', 'P', 'E', 'N', 'R', 'F', 'S', 'S', 'Y', 'S', ' '};
    static const uint8_t data_label[11] =
        {'O', 'P', 'E', 'N', 'R', 'F', 'S', 'D', 'A', 'T', 'A'};
    const uint8_t *label = volume == OPENRFSFS_VOLUME_SYSTEM ?
        system_label : data_label;
    uint32_t identifier = volume == OPENRFSFS_VOLUME_SYSTEM ?
        FAT32_SYSTEM_VOLUME_ID : FAT32_DATA_VOLUME_ID;

    return geometry->bytes_per_sector == OPENRFSFS_SECTOR_BYTES &&
        geometry->sectors_per_cluster == 1U &&
        geometry->reserved_sectors == OPENRFSFS_RESERVED_SECTORS &&
        geometry->fat_copies == 2U &&
        geometry->total_sectors == OPENRFSFS_VOLUME_SECTORS &&
        geometry->fat_sectors == OPENRFSFS_FAT_SECTORS &&
        geometry->root_cluster == OPENRFSFS_ROOT_CLUSTER &&
        geometry->volume_id == identifier &&
        equal_bytes(geometry->volume_label, label, sizeof(system_label)) ?
        OPENRFSFS_STATUS_OK : OPENRFSFS_STATUS_CORRUPT;
}

static enum openrfsfs_status validate_fats(
    struct nvme_volume_session *session,
    const struct fat32_geometry *geometry,
    uint64_t *free_clusters,
    uint32_t *next_free
)
{
    uint8_t first[OPENRFSFS_SECTOR_BYTES];
    uint8_t second[OPENRFSFS_SECTOR_BYTES];
    uint64_t free_count = 0U;
    uint32_t first_free = 0U;

    if (session == NULL || geometry == NULL || free_clusters == NULL ||
        next_free == NULL) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    for (uint64_t ordinal = 0U; ordinal < geometry->fat_sectors; ++ordinal) {
        enum openrfsfs_status status = direct_read(session,
            geometry->first_fat_sector + ordinal, first);

        if (status != OPENRFSFS_STATUS_OK) {
            return status;
        }
        status = direct_read(session, geometry->first_fat_sector +
            geometry->fat_sectors + ordinal, second);
        if (status != OPENRFSFS_STATUS_OK) {
            return status;
        }
        if (!equal_bytes(first, second, sizeof(first))) {
            return OPENRFSFS_STATUS_CORRUPT;
        }
        if (ordinal == 0U && openrfs_fat32_validate_fat_pair(first,
                sizeof(first), second, sizeof(second), geometry) !=
                FAT32_STATUS_OK) {
            return OPENRFSFS_STATUS_CORRUPT;
        }
        for (size_t offset = 0U; offset < sizeof(first); offset += 4U) {
            uint64_t cluster64 = ordinal * (OPENRFSFS_SECTOR_BYTES / 4U) +
                offset / 4U;
            uint32_t value;

            if (cluster64 < 2U || cluster64 > geometry->maximum_cluster) {
                continue;
            }
            value = read_u32(&first[offset]) & UINT32_C(0x0FFFFFFF);
            if (value == 0U) {
                ++free_count;
                if (first_free == 0U) {
                    first_free = (uint32_t)cluster64;
                }
            } else if (value == FAT32_BAD || value < 2U ||
                (value > geometry->maximum_cluster &&
                    value < FAT32_EOC_MIN)) {
                return OPENRFSFS_STATUS_CORRUPT;
            }
        }
    }
    *free_clusters = free_count;
    *next_free = first_free == 0U ? UINT32_MAX : first_free;
    return OPENRFSFS_STATUS_OK;
}

static bool validation_cluster_seen(uint32_t cluster)
{
    return (validation_seen[cluster / 8U] &
        (uint8_t)(UINT8_C(1) << (cluster % 8U))) != 0U;
}

static enum openrfsfs_status validation_claim_cluster(uint32_t cluster)
{
    if (cluster < 2U || cluster > OPENRFSFS_MAX_MEDIA_CLUSTER ||
        validation_cluster_seen(cluster)) {
        return OPENRFSFS_STATUS_CORRUPT;
    }
    validation_seen[cluster / 8U] |=
        (uint8_t)(UINT8_C(1) << (cluster % 8U));
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status validate_file_chain(
    struct openrfsfs_operation *operation,
    uint32_t first_cluster,
    uint32_t file_size
)
{
    uint32_t expected = (uint32_t)(((uint64_t)file_size +
        OPENRFSFS_SECTOR_BYTES - 1U) / OPENRFSFS_SECTOR_BYTES);
    uint32_t cluster = first_cluster;

    if (file_size > OPENRFSFS_MAX_FILE_BYTES ||
        (expected == 0U) != (first_cluster == 0U) ||
        expected > OPENRFSFS_MAX_CHAIN_CLUSTERS) {
        return OPENRFSFS_STATUS_CORRUPT;
    }
    for (uint32_t count = 0U; count < expected; ++count) {
        uint32_t next;
        bool end;
        enum openrfsfs_status status = validation_claim_cluster(cluster);

        if (status != OPENRFSFS_STATUS_OK) {
            return status;
        }
        status = next_cluster(operation, cluster, &next, &end);
        if (status != OPENRFSFS_STATUS_OK || end != (count + 1U == expected)) {
            return OPENRFSFS_STATUS_CORRUPT;
        }
        cluster = next;
    }
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status validation_enqueue_directory(
    uint32_t cluster,
    uint32_t parent,
    uint32_t depth,
    size_t *tail
)
{
    if (tail == NULL || depth > OPENRFSFS_MAX_DEPTH ||
        *tail >= OPENRFSFS_MAX_VALIDATION_DIRECTORIES) {
        return OPENRFSFS_STATUS_CORRUPT;
    }
    validation_directories[*tail].cluster = cluster;
    validation_directories[*tail].parent = parent;
    validation_directories[*tail].depth = depth;
    ++*tail;
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status validate_directory_tree(
    struct openrfsfs_operation *operation,
    const struct openrfsfs_validation_directory *directory,
    size_t *tail
)
{
    static const uint8_t dot[11] =
        {'.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
    static const uint8_t dotdot[11] =
        {'.', '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
    uint8_t directory_bytes[OPENRFSFS_SECTOR_BYTES];
    uint8_t names[OPENRFSFS_MAX_LIST_ENTRIES][FAT32_SHORT_NAME_BYTES];
    uint32_t cluster = directory->cluster;
    size_t name_count = 0U;
    bool dot_seen = false;
    bool dotdot_seen = false;

    for (uint32_t steps = 0U;
         steps < OPENRFSFS_MAX_CHAIN_CLUSTERS; ++steps) {
        uint64_t sector_number;
        struct openrfsfs_cache_entry *sector;
        uint32_t next;
        bool chain_end;
        bool directory_end = false;
        enum openrfsfs_status status = validation_claim_cluster(cluster);

        if (status != OPENRFSFS_STATUS_OK) {
            return status;
        }
        status = cluster_sector(operation->mount, cluster, &sector_number);
        if (status == OPENRFSFS_STATUS_OK) {
            status = cache_sector(operation, sector_number, &sector);
        }
        if (status != OPENRFSFS_STATUS_OK) {
            return status;
        }
        copy_bytes(directory_bytes, sector->data, sizeof(directory_bytes));
        for (uint32_t offset = 0U; offset < OPENRFSFS_SECTOR_BYTES;
             offset += FAT32_DIRECTORY_ENTRY_BYTES) {
            struct fat32_directory_entry entry;

            if (openrfs_fat32_parse_directory_entry(&directory_bytes[offset],
                    FAT32_DIRECTORY_ENTRY_BYTES, &entry) != FAT32_STATUS_OK) {
                return OPENRFSFS_STATUS_CORRUPT;
            }
            if (entry.kind == FAT32_ENTRY_END) {
                directory_end = true;
                break;
            }
            if (entry.kind != FAT32_ENTRY_ORDINARY ||
                (entry.attributes & OPENRFSFS_ATTR_VOLUME) != 0U) {
                continue;
            }
            if (name_equal(entry.short_name, dot)) {
                if (directory->depth == 0U || dot_seen ||
                    (entry.attributes & OPENRFSFS_ATTR_DIRECTORY) == 0U ||
                    entry.first_cluster != directory->cluster ||
                    entry.size != 0U) {
                    return OPENRFSFS_STATUS_CORRUPT;
                }
                dot_seen = true;
                continue;
            }
            if (name_equal(entry.short_name, dotdot)) {
                if (directory->depth == 0U || dotdot_seen ||
                    (entry.attributes & OPENRFSFS_ATTR_DIRECTORY) == 0U ||
                    entry.first_cluster != directory->parent ||
                    entry.size != 0U) {
                    return OPENRFSFS_STATUS_CORRUPT;
                }
                dotdot_seen = true;
                continue;
            }
            if (name_count >= OPENRFSFS_MAX_LIST_ENTRIES) {
                return OPENRFSFS_STATUS_CORRUPT;
            }
            for (size_t index = 0U; index < name_count; ++index) {
                if (name_equal(names[index], entry.short_name)) {
                    return OPENRFSFS_STATUS_CORRUPT;
                }
            }
            copy_bytes(names[name_count++], entry.short_name,
                FAT32_SHORT_NAME_BYTES);
            if ((entry.attributes & OPENRFSFS_ATTR_DIRECTORY) != 0U) {
                if (entry.size != 0U || entry.first_cluster < 2U) {
                    return OPENRFSFS_STATUS_CORRUPT;
                }
                status = validation_enqueue_directory(entry.first_cluster,
                    directory->cluster, directory->depth + 1U, tail);
            } else {
                status = validate_file_chain(operation, entry.first_cluster,
                    entry.size);
            }
            if (status != OPENRFSFS_STATUS_OK) {
                return status;
            }
        }
        status = next_cluster(operation, cluster, &next, &chain_end);
        if (status != OPENRFSFS_STATUS_OK || (directory_end && !chain_end)) {
            return OPENRFSFS_STATUS_CORRUPT;
        }
        if (chain_end) {
            if (directory->depth != 0U && (!dot_seen || !dotdot_seen)) {
                return OPENRFSFS_STATUS_CORRUPT;
            }
            return OPENRFSFS_STATUS_OK;
        }
        cluster = next;
    }
    return OPENRFSFS_STATUS_CORRUPT;
}

static enum openrfsfs_status validate_live_tree(
    struct nvme_volume_session *session,
    struct openrfsfs_mount_state *mount,
    enum openrfsfs_volume volume
)
{
    struct openrfsfs_operation operation = {0};
    size_t head = 0U;
    size_t tail = 0U;
    enum openrfsfs_status status;

    if (session == NULL || mount == NULL ||
        mount->geometry.maximum_cluster != OPENRFSFS_MAX_MEDIA_CLUSTER) {
        return OPENRFSFS_STATUS_CORRUPT;
    }
    zero_bytes(validation_seen, sizeof(validation_seen));
    zero_bytes(validation_directories, sizeof(validation_directories));
    invalidate_cache(volume);
    operation.nvme = *session;
    operation.mount = mount;
    operation.volume = volume;
    operation.active = true;
    status = validation_enqueue_directory(mount->geometry.root_cluster,
        mount->geometry.root_cluster, 0U, &tail);
    while (status == OPENRFSFS_STATUS_OK && head < tail) {
        status = validate_directory_tree(&operation,
            &validation_directories[head++], &tail);
    }
    for (uint32_t cluster = 2U;
         status == OPENRFSFS_STATUS_OK &&
            cluster <= mount->geometry.maximum_cluster;
         ++cluster) {
        uint32_t value;

        status = fat_entry(&operation, cluster, &value);
        if (status == OPENRFSFS_STATUS_OK &&
            ((value != 0U) != validation_cluster_seen(cluster))) {
            status = OPENRFSFS_STATUS_CORRUPT;
        }
    }
    invalidate_cache(volume);
    return status;
}

enum openrfsfs_status fat32_backend_mount(enum openrfsfs_volume volume)
{
    uint8_t boot[OPENRFSFS_SECTOR_BYTES];
    uint8_t backup[OPENRFSFS_SECTOR_BYTES];
    uint8_t info[OPENRFSFS_SECTOR_BYTES];
    uint8_t backup_info[OPENRFSFS_SECTOR_BYTES];
    struct nvme_volume_session session;
    struct openrfsfs_mount_state candidate;
    enum nvme_status open_status;
    enum nvme_status close_status;
    enum openrfsfs_status status;
    const char *validation_stage = "boot sector";
    uint32_t controller;

    if (!valid_volume(volume)) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    if (mounts[volume].active) {
        return OPENRFSFS_STATUS_ALREADY_MOUNTED;
    }
    zero_bytes(&candidate, sizeof(candidate));
    zero_bytes(&session, sizeof(session));
    controller = volume == OPENRFSFS_VOLUME_SYSTEM ?
        OPENRFSFS_SYSTEM_CONTROLLER : OPENRFSFS_DATA_CONTROLLER;
    open_status = nvme_volume_open(&session, controller, false);
    if (open_status != NVME_STATUS_OK) {
        mounts[volume].present = false;
        mounts[volume].healthy = false;
        report_nvme_failure("mount open", open_status);
        return nvme_result(open_status);
    }
    candidate.present = true;
    candidate.controller_index = controller;
    candidate.read_only = volume == OPENRFSFS_VOLUME_SYSTEM;
    status = direct_read(&session, 0U, boot);
    if (status == OPENRFSFS_STATUS_OK &&
        openrfs_fat32_parse_bpb(boot, sizeof(boot), session.namespace_blocks,
            session.logical_block_bytes, &candidate.geometry) !=
            FAT32_STATUS_OK) {
        status = OPENRFSFS_STATUS_CORRUPT;
    }
    if (status == OPENRFSFS_STATUS_OK) {
        validation_stage = "volume identity";
        status = validate_volume_identity(volume, &candidate.geometry);
    }
    if (status == OPENRFSFS_STATUS_OK &&
        candidate.geometry.bytes_per_sector != OPENRFSFS_SECTOR_BYTES) {
        status = OPENRFSFS_STATUS_CORRUPT;
    }
    if (status == OPENRFSFS_STATUS_OK) {
        validation_stage = "backup boot sector";
        status = direct_read(&session, candidate.geometry.backup_boot_sector,
            backup);
        if (status == OPENRFSFS_STATUS_OK &&
            !equal_bytes(boot, backup, sizeof(boot))) {
            status = OPENRFSFS_STATUS_CORRUPT;
        }
    }
    if (status == OPENRFSFS_STATUS_OK) {
        validation_stage = "FSInfo";
        status = direct_read(&session, candidate.geometry.fsinfo_sector,
            info);
        if (status == OPENRFSFS_STATUS_OK &&
            openrfs_fat32_parse_fsinfo(info, sizeof(info),
                &candidate.geometry, &candidate.fsinfo) != FAT32_STATUS_OK) {
            status = OPENRFSFS_STATUS_CORRUPT;
        }
    }
    if (status == OPENRFSFS_STATUS_OK) {
        validation_stage = "backup FSInfo";
        status = direct_read(&session,
            (uint64_t)candidate.geometry.backup_boot_sector +
                candidate.geometry.fsinfo_sector, backup_info);
        if (status == OPENRFSFS_STATUS_OK &&
            !equal_bytes(info, backup_info, sizeof(info))) {
            status = OPENRFSFS_STATUS_CORRUPT;
        }
    }
    if (status == OPENRFSFS_STATUS_OK) {
        validation_stage = "FAT copies";
        status = validate_fats(&session, &candidate.geometry,
            &candidate.free_clusters, &candidate.next_free);
    }
    if (status == OPENRFSFS_STATUS_OK) {
        validation_stage = "live tree";
        candidate.generation = UINT64_C(1);
        status = validate_live_tree(&session, &candidate, volume);
    }
    close_status = nvme_volume_close(&session);
    if (close_status != NVME_STATUS_OK && status == OPENRFSFS_STATUS_OK) {
        validation_stage = "controller close";
        status = OPENRFSFS_STATUS_IO;
    }
    invalidate_cache(volume);
    if (status != OPENRFSFS_STATUS_OK) {
        console_write("OpenRFS: FAT32 ");
        console_write(volume == OPENRFSFS_VOLUME_SYSTEM ? "system" : "data");
        console_write(" mount refused at ");
        console_write(validation_stage);
        console_write(": ");
        console_write(openrfsfs_status_string(status));
        console_write("\n");
        mounts[volume].present = candidate.present;
        mounts[volume].healthy = false;
        return status;
    }
    candidate.generation = next_mount_generation++;
    if (candidate.generation == 0U) {
        candidate.generation = next_mount_generation++;
    }
    candidate.active = true;
    candidate.healthy = true;
    mounts[volume] = candidate;
    return OPENRFSFS_STATUS_OK;
}

static enum openrfsfs_status write_fsinfo(struct openrfsfs_operation *operation)
{
    struct openrfsfs_cache_entry *primary;
    struct openrfsfs_cache_entry *backup;
    uint32_t free_hint;
    enum openrfsfs_status status;

    if (operation == NULL || !operation->writable) {
        return OPENRFSFS_STATUS_READ_ONLY;
    }
    free_hint = operation->mount->free_clusters > UINT32_MAX ? UINT32_MAX :
        (uint32_t)operation->mount->free_clusters;
    status = cache_sector(operation,
        operation->mount->geometry.fsinfo_sector, &primary);
    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    write_u32(&primary->data[OPENRFSFS_FSINFO_FREE_OFFSET], free_hint);
    write_u32(&primary->data[OPENRFSFS_FSINFO_NEXT_OFFSET],
        operation->mount->next_free);
    write_u32(&primary->data[OPENRFSFS_FSINFO_TRAIL_OFFSET], UINT32_C(0xAA550000));
    status = flush_one(operation, primary);
    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    status = cache_sector(operation,
        (uint64_t)operation->mount->geometry.backup_boot_sector +
            operation->mount->geometry.fsinfo_sector, &backup);
    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    copy_bytes(backup->data, primary->data, sizeof(backup->data));
    return flush_one(operation, backup);
}

enum openrfsfs_status fat32_backend_sync(enum openrfsfs_volume volume)
{
    struct openrfsfs_operation operation;
    enum openrfsfs_status status;
    enum openrfsfs_status close_status;

    if (!valid_volume(volume)) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    if (!mounts[volume].active) {
        return OPENRFSFS_STATUS_NOT_MOUNTED;
    }
    if (mounts[volume].read_only) {
        invalidate_cache(volume);
        return OPENRFSFS_STATUS_OK;
    }
    status = begin_operation(volume, true, &operation);
    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    status = flush_cache(&operation);
    if (status == OPENRFSFS_STATUS_OK) {
        status = write_fsinfo(&operation);
    }
    close_status = end_operation(&operation, status == OPENRFSFS_STATUS_OK);
    return status != OPENRFSFS_STATUS_OK ? status : close_status;
}

enum openrfsfs_status fat32_backend_unmount(enum openrfsfs_volume volume)
{
    enum openrfsfs_status status;

    if (!valid_volume(volume)) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    if (!mounts[volume].active) {
        return OPENRFSFS_STATUS_NOT_MOUNTED;
    }
    for (size_t index = 0U; index < OPENRFSFS_MAX_HANDLES; ++index) {
        if (handles[index].active && handles[index].volume == volume) {
            return OPENRFSFS_STATUS_BUSY;
        }
    }
    status = fat32_backend_sync(volume);
    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    invalidate_cache(volume);
    zero_bytes(&mounts[volume], sizeof(mounts[volume]));
    return OPENRFSFS_STATUS_OK;
}

void fat32_backend_initialize(void)
{
    zero_bytes(mounts, sizeof(mounts));
    zero_bytes(handles, sizeof(handles));
    zero_bytes(cache, sizeof(cache));
    cache_stamp = 0U;
    (void)fat32_backend_mount(OPENRFSFS_VOLUME_SYSTEM);
    (void)fat32_backend_mount(OPENRFSFS_VOLUME_DATA);
}

struct openrfsfs_drive_info fat32_backend_drive(enum openrfsfs_volume volume)
{
    struct openrfsfs_drive_info result = {0};
    struct openrfsfs_mount_state *mount;

    result.volume = volume;
    if (!valid_volume(volume)) {
        return result;
    }
    mount = &mounts[volume];
    result.volume_id = mount->geometry.volume_id;
    result.present = mount->present;
    result.mounted = mount->active;
    result.read_only = mount->read_only;
    result.healthy = mount->healthy;
    (void)multiply_u64(mount->geometry.total_sectors,
        mount->geometry.bytes_per_sector, &result.total_bytes);
    (void)multiply_u64(mount->free_clusters,
        (uint64_t)mount->geometry.bytes_per_sector *
            mount->geometry.sectors_per_cluster, &result.free_bytes);
    return result;
}

uint64_t fat32_backend_completion_count(enum openrfsfs_volume volume)
{
    return valid_volume(volume) ? mounts[volume].completion_count : 0U;
}

static openrfsfs_handle encode_handle(size_t index, uint64_t generation)
{
    return (generation << 8U) | (uint64_t)(index + 1U);
}

static enum openrfsfs_status handle_state(
    openrfsfs_handle handle,
    struct openrfsfs_handle_state **state
)
{
    uint64_t encoded_index = handle & UINT64_C(0xFF);
    uint64_t generation = handle >> 8U;
    size_t index;

    if (state == NULL || encoded_index == 0U ||
        encoded_index > OPENRFSFS_MAX_HANDLES || generation == 0U) {
        return OPENRFSFS_STATUS_STALE_HANDLE;
    }
    index = (size_t)(encoded_index - 1U);
    if (!handles[index].active || handles[index].generation != generation ||
        !mounts[handles[index].volume].active ||
        handles[index].mount_generation !=
            mounts[handles[index].volume].generation) {
        return OPENRFSFS_STATUS_STALE_HANDLE;
    }
    *state = &handles[index];
    return OPENRFSFS_STATUS_OK;
}

enum openrfsfs_status fat32_backend_open(
    enum openrfsfs_volume volume,
    const char *path,
    enum openrfsfs_access access,
    openrfsfs_handle *handle
)
{
    struct openrfsfs_operation operation;
    struct openrfsfs_location location;
    enum openrfsfs_status status;
    enum openrfsfs_status close_status;
    size_t slot = OPENRFSFS_MAX_HANDLES;

    if (handle == NULL || !valid_volume(volume) ||
        (access != OPENRFSFS_ACCESS_READ && access != OPENRFSFS_ACCESS_WRITE &&
            access != OPENRFSFS_ACCESS_READ_WRITE)) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    *handle = 0U;
    if ((access & OPENRFSFS_ACCESS_WRITE) != 0U && mounts[volume].read_only) {
        return OPENRFSFS_STATUS_READ_ONLY;
    }
    for (size_t index = 0U; index < OPENRFSFS_MAX_HANDLES; ++index) {
        if (!handles[index].active) {
            slot = index;
            break;
        }
    }
    if (slot == OPENRFSFS_MAX_HANDLES) {
        return OPENRFSFS_STATUS_NO_HANDLES;
    }
    status = begin_operation(volume, false, &operation);
    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    status = resolve_path(&operation, path, &location);
    close_status = end_operation(&operation, false);
    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    if (close_status != OPENRFSFS_STATUS_OK) {
        return close_status;
    }
    if ((location.entry.attributes & OPENRFSFS_ATTR_DIRECTORY) != 0U) {
        return OPENRFSFS_STATUS_IS_DIRECTORY;
    }
    zero_bytes(&handles[slot], sizeof(handles[slot]));
    handles[slot].generation = next_handle_generation++;
    if (handles[slot].generation == 0U ||
        handles[slot].generation > (UINT64_MAX >> 8U)) {
        next_handle_generation = 2U;
        handles[slot].generation = 1U;
    }
    handles[slot].mount_generation = mounts[volume].generation;
    handles[slot].entry_sector = location.sector;
    handles[slot].entry_offset = location.offset;
    handles[slot].first_cluster = location.entry.first_cluster;
    handles[slot].size = location.entry.size;
    handles[slot].volume = volume;
    handles[slot].access = access;
    handles[slot].active = true;
    *handle = encode_handle(slot, handles[slot].generation);
    return OPENRFSFS_STATUS_OK;
}

enum openrfsfs_status fat32_backend_close(openrfsfs_handle handle)
{
    struct openrfsfs_handle_state *state;
    enum openrfsfs_status status = handle_state(handle, &state);

    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    state->active = false;
    state->mount_generation = 0U;
    state->offset = 0U;
    return OPENRFSFS_STATUS_OK;
}

enum openrfsfs_status fat32_backend_seek(
    openrfsfs_handle handle,
    int64_t offset,
    enum openrfsfs_seek_origin origin,
    uint64_t *position
)
{
    struct openrfsfs_handle_state *state;
    uint64_t base;
    uint64_t target;
    enum openrfsfs_status status;

    if (position == NULL) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    *position = 0U;
    status = handle_state(handle, &state);
    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    if (origin == OPENRFSFS_SEEK_START) {
        base = 0U;
    } else if (origin == OPENRFSFS_SEEK_CURRENT) {
        base = state->offset;
    } else if (origin == OPENRFSFS_SEEK_END) {
        base = state->size;
    } else {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    if (offset < 0) {
        uint64_t magnitude = (uint64_t)(-(offset + 1)) + 1U;

        if (magnitude > base) {
            return OPENRFSFS_STATUS_RANGE;
        }
        target = base - magnitude;
    } else if (!add_u64(base, (uint64_t)offset, &target)) {
        return OPENRFSFS_STATUS_RANGE;
    }
    if (target > OPENRFSFS_MAX_FILE_BYTES) {
        return OPENRFSFS_STATUS_RANGE;
    }
    state->offset = (uint32_t)target;
    *position = state->offset;
    return OPENRFSFS_STATUS_OK;
}

enum openrfsfs_status fat32_backend_read(
    openrfsfs_handle handle,
    uint8_t *destination,
    size_t capacity,
    size_t *read_bytes
)
{
    struct openrfsfs_handle_state *state;
    struct openrfsfs_operation operation;
    size_t completed = 0U;
    size_t available;
    enum openrfsfs_status status;
    enum openrfsfs_status close_status;

    if (read_bytes == NULL || (capacity != 0U && destination == NULL)) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    *read_bytes = 0U;
    status = handle_state(handle, &state);
    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    if ((state->access & OPENRFSFS_ACCESS_READ) == 0U) {
        return OPENRFSFS_STATUS_ACCESS;
    }
    if (state->offset >= state->size || capacity == 0U) {
        return OPENRFSFS_STATUS_OK;
    }
    available = state->size - state->offset;
    if (capacity < available) {
        available = capacity;
    }
    status = begin_operation(state->volume, false, &operation);
    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    while (completed < available) {
        uint32_t file_offset = state->offset + (uint32_t)completed;
        uint32_t ordinal = file_offset / OPENRFSFS_SECTOR_BYTES;
        uint32_t within = file_offset % OPENRFSFS_SECTOR_BYTES;
        uint32_t cluster;
        uint64_t sector_number;
        struct openrfsfs_cache_entry *sector;
        size_t chunk = OPENRFSFS_SECTOR_BYTES - within;

        if (chunk > available - completed) {
            chunk = available - completed;
        }
        status = chain_cluster_at(&operation, state->first_cluster, ordinal,
            &cluster);
        if (status != OPENRFSFS_STATUS_OK) {
            break;
        }
        status = cluster_sector(operation.mount, cluster, &sector_number);
        if (status != OPENRFSFS_STATUS_OK) {
            break;
        }
        status = cache_sector(&operation, sector_number, &sector);
        if (status != OPENRFSFS_STATUS_OK) {
            break;
        }
        copy_bytes(destination + completed, &sector->data[within], chunk);
        completed += chunk;
    }
    close_status = end_operation(&operation, false);
    state->offset += (uint32_t)completed;
    *read_bytes = completed;
    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    return close_status;
}

enum openrfsfs_status fat32_backend_pread(
    openrfsfs_handle handle,
    uint8_t *destination,
    size_t capacity,
    uint64_t offset,
    size_t *read_bytes
)
{
    struct openrfsfs_handle_state *state;
    uint32_t saved;
    enum openrfsfs_status status = handle_state(handle, &state);

    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    if (offset > OPENRFSFS_MAX_FILE_BYTES) {
        return OPENRFSFS_STATUS_RANGE;
    }
    saved = state->offset;
    state->offset = (uint32_t)offset;
    status = fat32_backend_read(handle, destination, capacity, read_bytes);
    state->offset = saved;
    return status;
}

static enum openrfsfs_status handle_location(
    struct openrfsfs_operation *operation,
    const struct openrfsfs_handle_state *state,
    struct openrfsfs_location *location
)
{
    struct openrfsfs_cache_entry *sector;
    enum fat32_status parsed;
    enum openrfsfs_status status;

    if (operation == NULL || state == NULL || location == NULL) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    zero_bytes(location, sizeof(*location));
    status = cache_sector(operation, state->entry_sector, &sector);

    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    parsed = openrfs_fat32_parse_directory_entry(
        &sector->data[state->entry_offset], FAT32_DIRECTORY_ENTRY_BYTES,
        &location->entry);
    if (parsed != FAT32_STATUS_OK ||
        location->entry.kind != FAT32_ENTRY_ORDINARY ||
        location->entry.first_cluster != state->first_cluster ||
        location->entry.size != state->size) {
        return OPENRFSFS_STATUS_STALE_HANDLE;
    }
    location->sector = state->entry_sector;
    location->offset = state->entry_offset;
    location->found = true;
    return OPENRFSFS_STATUS_OK;
}

enum openrfsfs_status fat32_backend_write(
    openrfsfs_handle handle,
    const uint8_t *source,
    size_t source_bytes,
    size_t *written_bytes
)
{
    struct openrfsfs_handle_state *state;
    struct openrfsfs_operation operation;
    struct openrfsfs_location location;
    uint64_t end_offset;
    uint32_t required;
    uint32_t first;
    size_t completed = 0U;
    enum openrfsfs_status status;
    enum openrfsfs_status close_status;

    if (written_bytes == NULL || (source_bytes != 0U && source == NULL)) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    *written_bytes = 0U;
    status = handle_state(handle, &state);
    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    if ((state->access & OPENRFSFS_ACCESS_WRITE) == 0U) {
        return OPENRFSFS_STATUS_ACCESS;
    }
    if (!add_u64(state->offset, source_bytes, &end_offset) ||
        end_offset > OPENRFSFS_MAX_FILE_BYTES) {
        return OPENRFSFS_STATUS_RANGE;
    }
    if (source_bytes == 0U) {
        return OPENRFSFS_STATUS_OK;
    }
    status = begin_operation(state->volume, true, &operation);
    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    status = handle_location(&operation, state, &location);
    required = ((uint32_t)end_offset + OPENRFSFS_SECTOR_BYTES - 1U) /
        OPENRFSFS_SECTOR_BYTES;
    first = state->first_cluster;
    if (status == OPENRFSFS_STATUS_OK) {
        status = ensure_chain(&operation, first, required, &first);
    }
    while (status == OPENRFSFS_STATUS_OK && completed < source_bytes) {
        uint32_t file_offset = state->offset + (uint32_t)completed;
        uint32_t ordinal = file_offset / OPENRFSFS_SECTOR_BYTES;
        uint32_t within = file_offset % OPENRFSFS_SECTOR_BYTES;
        uint32_t cluster;
        uint64_t sector_number;
        struct openrfsfs_cache_entry *sector;
        size_t chunk = OPENRFSFS_SECTOR_BYTES - within;

        if (chunk > source_bytes - completed) {
            chunk = source_bytes - completed;
        }
        status = chain_cluster_at(&operation, first, ordinal, &cluster);
        if (status != OPENRFSFS_STATUS_OK) {
            break;
        }
        status = cluster_sector(operation.mount, cluster, &sector_number);
        if (status != OPENRFSFS_STATUS_OK) {
            break;
        }
        status = cache_sector(&operation, sector_number, &sector);
        if (status != OPENRFSFS_STATUS_OK) {
            break;
        }
        copy_bytes(&sector->data[within], source + completed, chunk);
        status = flush_one(&operation, sector);
        if (status == OPENRFSFS_STATUS_OK) {
            completed += chunk;
        }
    }
    if (status == OPENRFSFS_STATUS_OK) {
        uint32_t new_size = state->size;

        if (end_offset > new_size) {
            new_size = (uint32_t)end_offset;
        }
        status = update_location(&operation, &location, first, new_size);
        if (status == OPENRFSFS_STATUS_OK) {
            state->first_cluster = first;
            state->size = new_size;
            state->offset += (uint32_t)completed;
        }
    }
    close_status = end_operation(&operation, status == OPENRFSFS_STATUS_OK);
    *written_bytes = completed;
    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    return close_status;
}

enum openrfsfs_status fat32_backend_stat_path(
    enum openrfsfs_volume volume,
    const char *path,
    struct openrfsfs_stat *stat
)
{
    struct openrfsfs_operation operation;
    struct openrfsfs_location location;
    uint32_t count;
    uint32_t last;
    enum openrfsfs_status status;
    enum openrfsfs_status close_status;

    if (stat == NULL) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    zero_bytes(stat, sizeof(*stat));
    status = begin_operation(volume, false, &operation);
    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    status = resolve_path(&operation, path, &location);
    if (status == OPENRFSFS_STATUS_OK) {
        status = count_chain(&operation, location.entry.first_cluster,
            &count, &last);
        (void)last;
    }
    if (status == OPENRFSFS_STATUS_OK) {
        stat->size = location.entry.size;
        stat->first_cluster = location.entry.first_cluster;
        stat->cluster_count = count;
        stat->attributes = location.entry.attributes;
        stat->directory =
            (location.entry.attributes & OPENRFSFS_ATTR_DIRECTORY) != 0U;
        stat->read_only = operation.mount->read_only ||
            (location.entry.attributes & OPENRFSFS_ATTR_READ_ONLY) != 0U;
    }
    close_status = end_operation(&operation, false);
    return status != OPENRFSFS_STATUS_OK ? status : close_status;
}

static void display_name(const uint8_t *short_name, char *output)
{
    size_t used = 0U;
    size_t base = 8U;
    size_t extension = 3U;

    while (base != 0U && short_name[base - 1U] == ' ') {
        --base;
    }
    while (extension != 0U && short_name[8U + extension - 1U] == ' ') {
        --extension;
    }
    for (size_t index = 0U; index < base; ++index) {
        uint8_t byte = short_name[index];
        output[used++] = (char)(byte >= 'A' && byte <= 'Z' ?
            byte - 'A' + 'a' : byte);
    }
    if (extension != 0U) {
        output[used++] = '.';
        for (size_t index = 0U; index < extension; ++index) {
            uint8_t byte = short_name[8U + index];
            output[used++] = (char)(byte >= 'A' && byte <= 'Z' ?
                byte - 'A' + 'a' : byte);
        }
    }
    output[used] = '\0';
}

enum openrfsfs_status fat32_backend_list(
    enum openrfsfs_volume volume,
    const char *path,
    struct openrfsfs_list_entry *entries,
    size_t capacity,
    size_t *entry_count
)
{
    struct openrfsfs_operation operation;
    struct openrfsfs_location directory;
    uint32_t cluster;
    size_t count = 0U;
    enum openrfsfs_status status;
    enum openrfsfs_status close_status;

    if (entry_count == NULL || (capacity != 0U && entries == NULL)) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    *entry_count = 0U;
    status = begin_operation(volume, false, &operation);
    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    status = resolve_path(&operation, path, &directory);
    if (status == OPENRFSFS_STATUS_OK &&
        (directory.entry.attributes & OPENRFSFS_ATTR_DIRECTORY) == 0U) {
        status = OPENRFSFS_STATUS_NOT_DIRECTORY;
    }
    if (status == OPENRFSFS_STATUS_OK) {
        cluster = directory.entry.first_cluster;
    }
    for (uint32_t steps = 0U;
         status == OPENRFSFS_STATUS_OK && steps < OPENRFSFS_MAX_CHAIN_CLUSTERS;
         ++steps) {
        uint64_t first_sector;
        uint32_t next;
        bool end;

        status = cluster_sector(operation.mount, cluster, &first_sector);
        if (status != OPENRFSFS_STATUS_OK) {
            break;
        }
        for (uint32_t sector_index = 0U;
             status == OPENRFSFS_STATUS_OK &&
                sector_index < operation.mount->geometry.sectors_per_cluster;
             ++sector_index) {
            struct openrfsfs_cache_entry *sector;

            status = cache_sector(&operation, first_sector + sector_index,
                &sector);
            if (status != OPENRFSFS_STATUS_OK) {
                break;
            }
            for (uint32_t offset = 0U; offset < OPENRFSFS_SECTOR_BYTES;
                 offset += FAT32_DIRECTORY_ENTRY_BYTES) {
                struct fat32_directory_entry parsed;
                enum fat32_status parse_status =
                    openrfs_fat32_parse_directory_entry(&sector->data[offset],
                        FAT32_DIRECTORY_ENTRY_BYTES, &parsed);

                if (parse_status != FAT32_STATUS_OK) {
                    status = OPENRFSFS_STATUS_CORRUPT;
                    break;
                }
                if (parsed.kind == FAT32_ENTRY_END) {
                    end = true;
                    goto listing_complete;
                }
                if (parsed.kind != FAT32_ENTRY_ORDINARY ||
                    (parsed.attributes & OPENRFSFS_ATTR_VOLUME) != 0U ||
                    name_equal(parsed.short_name, (const uint8_t *)".          ") ||
                    name_equal(parsed.short_name, (const uint8_t *)"..         ")) {
                    continue;
                }
                if (count >= capacity || count >= OPENRFSFS_MAX_LIST_ENTRIES) {
                    status = OPENRFSFS_STATUS_RANGE;
                    goto listing_complete;
                }
                display_name(parsed.short_name, entries[count].name);
                entries[count].size = parsed.size;
                entries[count].attributes = parsed.attributes;
                entries[count].directory =
                    (parsed.attributes & OPENRFSFS_ATTR_DIRECTORY) != 0U;
                ++count;
            }
        }
        if (status != OPENRFSFS_STATUS_OK) {
            break;
        }
        status = next_cluster(&operation, cluster, &next, &end);
        if (status != OPENRFSFS_STATUS_OK || end) {
            break;
        }
        cluster = next;
    }
listing_complete:
    *entry_count = count;
    close_status = end_operation(&operation, false);
    if (status == OPENRFSFS_STATUS_OK && close_status != OPENRFSFS_STATUS_OK) {
        status = close_status;
    }
    return status;
}

enum openrfsfs_status fat32_backend_create(
    enum openrfsfs_volume volume,
    const char *path,
    uint16_t mode
)
{
    (void)mode;
    struct openrfsfs_operation operation;
    struct openrfsfs_parent parent;
    struct openrfsfs_free_slot slot;
    enum openrfsfs_status status = begin_operation(volume, true, &operation);
    enum openrfsfs_status close_status;

    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    status = resolve_parent(&operation, path, &parent);
    if (status == OPENRFSFS_STATUS_OK) {
        status = find_or_grow_slot(&operation, parent.cluster, &parent.name,
            &slot);
    }
    if (status == OPENRFSFS_STATUS_OK) {
        status = write_directory_entry(&operation, slot.sector, slot.offset,
            parent.name.canonical, UINT8_C(0x20), 0U, 0U);
    }
    close_status = end_operation(&operation, status == OPENRFSFS_STATUS_OK);
    return status != OPENRFSFS_STATUS_OK ? status : close_status;
}

enum openrfsfs_status fat32_backend_mkdir(
    enum openrfsfs_volume volume,
    const char *path
)
{
    static const uint8_t dot[11] =
        {'.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
    static const uint8_t dotdot[11] =
        {'.', '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
    struct openrfsfs_operation operation;
    struct openrfsfs_parent parent;
    struct openrfsfs_free_slot slot;
    struct openrfsfs_cache_entry *sector;
    uint32_t cluster = 0U;
    uint64_t sector_number;
    enum openrfsfs_status status = begin_operation(volume, true, &operation);
    enum openrfsfs_status close_status;

    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    status = resolve_parent(&operation, path, &parent);
    if (status == OPENRFSFS_STATUS_OK) {
        status = find_or_grow_slot(&operation, parent.cluster, &parent.name,
            &slot);
    }
    if (status == OPENRFSFS_STATUS_OK) {
        status = allocate_cluster(&operation, &cluster);
    }
    if (status == OPENRFSFS_STATUS_OK) {
        status = cluster_sector(operation.mount, cluster, &sector_number);
    }
    if (status == OPENRFSFS_STATUS_OK) {
        status = cache_sector(&operation, sector_number, &sector);
    }
    if (status == OPENRFSFS_STATUS_OK) {
        zero_bytes(sector->data, sizeof(sector->data));
        encode_directory_entry(&sector->data[0], dot, OPENRFSFS_ATTR_DIRECTORY,
            cluster, 0U);
        encode_directory_entry(&sector->data[FAT32_DIRECTORY_ENTRY_BYTES],
            dotdot, OPENRFSFS_ATTR_DIRECTORY, parent.cluster, 0U);
        status = flush_one(&operation, sector);
    }
    if (status == OPENRFSFS_STATUS_OK) {
        status = write_directory_entry(&operation, slot.sector, slot.offset,
            parent.name.canonical, OPENRFSFS_ATTR_DIRECTORY, cluster, 0U);
    }
    if (status != OPENRFSFS_STATUS_OK && cluster != 0U) {
        (void)release_chain(&operation, cluster);
    }
    close_status = end_operation(&operation, status == OPENRFSFS_STATUS_OK);
    return status != OPENRFSFS_STATUS_OK ? status : close_status;
}

static bool location_open(
    enum openrfsfs_volume volume,
    const struct openrfsfs_location *location
)
{
    for (size_t index = 0U; index < OPENRFSFS_MAX_HANDLES; ++index) {
        if (handles[index].active && handles[index].volume == volume &&
            handles[index].entry_sector == location->sector &&
            handles[index].entry_offset == location->offset) {
            return true;
        }
    }
    return false;
}

enum openrfsfs_status fat32_backend_unlink(
    enum openrfsfs_volume volume,
    const char *path
)
{
    struct openrfsfs_operation operation;
    struct openrfsfs_location location;
    enum openrfsfs_status status = begin_operation(volume, true, &operation);
    enum openrfsfs_status close_status;

    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    status = resolve_path(&operation, path, &location);
    if (status == OPENRFSFS_STATUS_OK &&
        (location.entry.attributes & OPENRFSFS_ATTR_DIRECTORY) != 0U) {
        status = OPENRFSFS_STATUS_IS_DIRECTORY;
    }
    if (status == OPENRFSFS_STATUS_OK && location_open(volume, &location)) {
        status = OPENRFSFS_STATUS_BUSY;
    }
    if (status == OPENRFSFS_STATUS_OK) {
        status = mark_deleted(&operation, &location);
    }
    if (status == OPENRFSFS_STATUS_OK) {
        status = release_chain(&operation, location.entry.first_cluster);
    }
    close_status = end_operation(&operation, status == OPENRFSFS_STATUS_OK);
    return status != OPENRFSFS_STATUS_OK ? status : close_status;
}

static enum openrfsfs_status directory_empty(
    struct openrfsfs_operation *operation,
    uint32_t directory_cluster,
    bool *empty
)
{
    uint32_t cluster = directory_cluster;

    if (empty == NULL) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    *empty = true;
    for (uint32_t steps = 0U; steps < OPENRFSFS_MAX_CHAIN_CLUSTERS; ++steps) {
        uint64_t first_sector;
        uint32_t next;
        bool end;
        enum openrfsfs_status status = cluster_sector(operation->mount, cluster,
            &first_sector);

        if (status != OPENRFSFS_STATUS_OK) {
            return status;
        }
        for (uint32_t sector_index = 0U;
             sector_index < operation->mount->geometry.sectors_per_cluster;
             ++sector_index) {
            struct openrfsfs_cache_entry *sector;

            status = cache_sector(operation, first_sector + sector_index,
                &sector);
            if (status != OPENRFSFS_STATUS_OK) {
                return status;
            }
            for (uint32_t offset = 0U; offset < OPENRFSFS_SECTOR_BYTES;
                 offset += FAT32_DIRECTORY_ENTRY_BYTES) {
                struct fat32_directory_entry parsed;

                if (openrfs_fat32_parse_directory_entry(&sector->data[offset],
                        FAT32_DIRECTORY_ENTRY_BYTES, &parsed) !=
                        FAT32_STATUS_OK) {
                    return OPENRFSFS_STATUS_CORRUPT;
                }
                if (parsed.kind == FAT32_ENTRY_END) {
                    return OPENRFSFS_STATUS_OK;
                }
                if (parsed.kind != FAT32_ENTRY_ORDINARY ||
                    (parsed.attributes & OPENRFSFS_ATTR_VOLUME) != 0U ||
                    name_equal(parsed.short_name, (const uint8_t *)".          ") ||
                    name_equal(parsed.short_name, (const uint8_t *)"..         ")) {
                    continue;
                }
                *empty = false;
                return OPENRFSFS_STATUS_OK;
            }
        }
        status = next_cluster(operation, cluster, &next, &end);
        if (status != OPENRFSFS_STATUS_OK || end) {
            return status;
        }
        cluster = next;
    }
    return OPENRFSFS_STATUS_CORRUPT;
}

enum openrfsfs_status fat32_backend_rmdir(
    enum openrfsfs_volume volume,
    const char *path
)
{
    struct openrfsfs_operation operation;
    struct openrfsfs_location location;
    bool empty = false;
    enum openrfsfs_status status = begin_operation(volume, true, &operation);
    enum openrfsfs_status close_status;

    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    status = resolve_path(&operation, path, &location);
    if (status == OPENRFSFS_STATUS_OK && location.root) {
        status = OPENRFSFS_STATUS_ACCESS;
    }
    if (status == OPENRFSFS_STATUS_OK &&
        (location.entry.attributes & OPENRFSFS_ATTR_DIRECTORY) == 0U) {
        status = OPENRFSFS_STATUS_NOT_DIRECTORY;
    }
    if (status == OPENRFSFS_STATUS_OK) {
        status = directory_empty(&operation, location.entry.first_cluster,
            &empty);
    }
    if (status == OPENRFSFS_STATUS_OK && !empty) {
        status = OPENRFSFS_STATUS_NOT_EMPTY;
    }
    if (status == OPENRFSFS_STATUS_OK) {
        status = mark_deleted(&operation, &location);
    }
    if (status == OPENRFSFS_STATUS_OK) {
        status = release_chain(&operation, location.entry.first_cluster);
    }
    close_status = end_operation(&operation, status == OPENRFSFS_STATUS_OK);
    return status != OPENRFSFS_STATUS_OK ? status : close_status;
}

enum openrfsfs_status fat32_backend_truncate(
    enum openrfsfs_volume volume,
    const char *path,
    uint64_t size
)
{
    struct openrfsfs_operation operation;
    struct openrfsfs_location location;
    uint32_t old_count;
    uint32_t old_last;
    uint32_t required;
    uint32_t first;
    uint32_t old_size;
    enum openrfsfs_status status;
    enum openrfsfs_status close_status;
    uint32_t checked_size;

    if (size > OPENRFSFS_MAX_FILE_BYTES) {
        return OPENRFSFS_STATUS_RANGE;
    }
    checked_size = (uint32_t)size;
    status = begin_operation(volume, true, &operation);
    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    status = resolve_path(&operation, path, &location);
    if (status == OPENRFSFS_STATUS_OK &&
        (location.entry.attributes & OPENRFSFS_ATTR_DIRECTORY) != 0U) {
        status = OPENRFSFS_STATUS_IS_DIRECTORY;
    }
    if (status == OPENRFSFS_STATUS_OK && location_open(volume, &location)) {
        status = OPENRFSFS_STATUS_BUSY;
    }
    first = status == OPENRFSFS_STATUS_OK ? location.entry.first_cluster : 0U;
    old_size = status == OPENRFSFS_STATUS_OK ? location.entry.size : 0U;
    required = (checked_size + OPENRFSFS_SECTOR_BYTES - 1U) /
        OPENRFSFS_SECTOR_BYTES;
    if (status == OPENRFSFS_STATUS_OK) {
        status = count_chain(&operation, first, &old_count, &old_last);
        (void)old_last;
    }
    if (status == OPENRFSFS_STATUS_OK && required > old_count) {
        status = ensure_chain(&operation, first, required, &first);
        if (status == OPENRFSFS_STATUS_OK) {
            status = zero_chain_bytes(&operation, first, old_size,
                checked_size);
        }
        if (status == OPENRFSFS_STATUS_OK) {
            status = update_location(&operation, &location, first,
                checked_size);
        }
    } else if (status == OPENRFSFS_STATUS_OK && required == 0U) {
        status = update_location(&operation, &location, 0U, 0U);
        if (status == OPENRFSFS_STATUS_OK) {
            status = release_chain(&operation, first);
        }
    } else if (status == OPENRFSFS_STATUS_OK && required < old_count) {
        uint32_t kept;
        uint32_t tail;
        bool end;

        status = chain_cluster_at(&operation, first, required - 1U, &kept);
        if (status == OPENRFSFS_STATUS_OK) {
            status = next_cluster(&operation, kept, &tail, &end);
        }
        if (status == OPENRFSFS_STATUS_OK && end) {
            status = OPENRFSFS_STATUS_CORRUPT;
        }
        if (status == OPENRFSFS_STATUS_OK) {
            status = zero_chain_bytes(&operation, first, checked_size,
                required * OPENRFSFS_SECTOR_BYTES);
        }
        if (status == OPENRFSFS_STATUS_OK) {
            status = update_location(&operation, &location, first,
                checked_size);
        }
        if (status == OPENRFSFS_STATUS_OK) {
            status = set_fat(&operation, kept, FAT32_EOC);
        }
        if (status == OPENRFSFS_STATUS_OK) {
            status = release_chain(&operation, tail);
        }
    } else if (status == OPENRFSFS_STATUS_OK) {
        status = checked_size >= old_size ?
            zero_chain_bytes(&operation, first, old_size, checked_size) :
            zero_chain_bytes(&operation, first, checked_size,
                required * OPENRFSFS_SECTOR_BYTES);
        if (status == OPENRFSFS_STATUS_OK) {
            status = update_location(&operation, &location, first,
                checked_size);
        }
    }
    close_status = end_operation(&operation, status == OPENRFSFS_STATUS_OK);
    return status != OPENRFSFS_STATUS_OK ? status : close_status;
}

enum openrfsfs_status fat32_backend_link(
    enum openrfsfs_volume volume,
    const char *source,
    const char *destination
)
{
    (void)volume;
    (void)source;
    (void)destination;
    return OPENRFSFS_STATUS_ACCESS;
}

static enum openrfsfs_status destination_inside_directory(
    struct openrfsfs_operation *operation,
    uint32_t directory_cluster,
    uint32_t destination_parent,
    bool *inside
)
{
    static const uint8_t dotdot[11] =
        {'.', '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
    uint32_t current = destination_parent;
    struct fat32_name parent_name = {0};

    if (operation == NULL || inside == NULL) {
        return OPENRFSFS_STATUS_INVALID_ARGUMENT;
    }
    *inside = false;
    copy_bytes(parent_name.canonical, dotdot, sizeof(dotdot));
    parent_name.kind = FAT32_NAME_ORDINARY;
    for (uint32_t depth = 0U; depth <= OPENRFSFS_MAX_DEPTH; ++depth) {
        struct openrfsfs_location parent_entry;
        enum openrfsfs_status status;

        if (current == directory_cluster) {
            *inside = true;
            return OPENRFSFS_STATUS_OK;
        }
        if (current == operation->mount->geometry.root_cluster) {
            return OPENRFSFS_STATUS_OK;
        }
        status = scan_directory(operation, current, &parent_name,
            &parent_entry, NULL);
        if (status != OPENRFSFS_STATUS_OK ||
            (parent_entry.entry.attributes & OPENRFSFS_ATTR_DIRECTORY) == 0U ||
            parent_entry.entry.first_cluster < 2U ||
            parent_entry.entry.first_cluster == current) {
            return OPENRFSFS_STATUS_CORRUPT;
        }
        current = parent_entry.entry.first_cluster;
    }
    return OPENRFSFS_STATUS_CORRUPT;
}

enum openrfsfs_status fat32_backend_rename(
    enum openrfsfs_volume volume,
    const char *source,
    const char *destination
)
{
    static const uint8_t dotdot[11] =
        {'.', '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
    struct openrfsfs_operation operation;
    struct openrfsfs_location source_location;
    struct openrfsfs_parent destination_parent;
    struct openrfsfs_free_slot destination_slot;
    bool inside = false;
    enum openrfsfs_status status = begin_operation(volume, true, &operation);
    enum openrfsfs_status close_status;

    if (status != OPENRFSFS_STATUS_OK) {
        return status;
    }
    status = resolve_path(&operation, source, &source_location);
    if (status == OPENRFSFS_STATUS_OK && source_location.root) {
        status = OPENRFSFS_STATUS_ACCESS;
    }
    if (status == OPENRFSFS_STATUS_OK &&
        location_open(volume, &source_location)) {
        status = OPENRFSFS_STATUS_BUSY;
    }
    if (status == OPENRFSFS_STATUS_OK) {
        status = resolve_parent(&operation, destination, &destination_parent);
    }
    if (status == OPENRFSFS_STATUS_OK &&
        (source_location.entry.attributes & OPENRFSFS_ATTR_DIRECTORY) != 0U) {
        status = destination_inside_directory(&operation,
            source_location.entry.first_cluster,
            destination_parent.cluster, &inside);
        if (status == OPENRFSFS_STATUS_OK && inside) {
            status = OPENRFSFS_STATUS_PATH;
        }
    }
    if (status == OPENRFSFS_STATUS_OK) {
        status = find_or_grow_slot(&operation, destination_parent.cluster,
            &destination_parent.name, &destination_slot);
    }
    if (status == OPENRFSFS_STATUS_OK &&
        (source_location.entry.attributes & OPENRFSFS_ATTR_DIRECTORY) != 0U) {
        struct fat32_name parent_name = {0};
        struct openrfsfs_location parent_entry;

        copy_bytes(parent_name.canonical, dotdot, sizeof(dotdot));
        parent_name.kind = FAT32_NAME_ORDINARY;
        status = scan_directory(&operation,
            source_location.entry.first_cluster, &parent_name,
            &parent_entry, NULL);
        if (status == OPENRFSFS_STATUS_OK) {
            status = update_location(&operation, &parent_entry,
                destination_parent.cluster, 0U);
        }
    }
    if (status == OPENRFSFS_STATUS_OK) {
        status = write_directory_entry(&operation, destination_slot.sector,
            destination_slot.offset, destination_parent.name.canonical,
            source_location.entry.attributes,
            source_location.entry.first_cluster, source_location.entry.size);
    }
    if (status == OPENRFSFS_STATUS_OK) {
        status = mark_deleted(&operation, &source_location);
    }
    close_status = end_operation(&operation, status == OPENRFSFS_STATUS_OK);
    return status != OPENRFSFS_STATUS_OK ? status : close_status;
}

bool fat32_backend_self_test(size_t *completed_tests)
{
    size_t completed = 0U;
    uint32_t components = 0U;
    struct fat32_name name;
    struct openrfsfs_cache_entry model[OPENRFSFS_CACHE_ENTRIES];

    if (completed_tests == NULL) {
        return false;
    }
    *completed_tests = 0U;
    if (openrfs_fat32_validate_path((const uint8_t *)"projects/notes.txt",
            18U, &components) != FAT32_STATUS_OK || components != 2U) {
        return false;
    }
    ++completed;
    if (openrfs_fat32_validate_path((const uint8_t *)"../escape", 9U,
            &components) != FAT32_STATUS_ABOVE_ROOT) {
        return false;
    }
    ++completed;
    if (openrfs_fat32_parse_component((const uint8_t *)"notes.txt", 9U,
            &name) != FAT32_STATUS_OK ||
        !equal_bytes(name.canonical, (const uint8_t *)"NOTES   TXT", 11U)) {
        return false;
    }
    ++completed;
    zero_bytes(model, sizeof(model));
    for (size_t index = 0U; index < OPENRFSFS_CACHE_ENTRIES; ++index) {
        model[index].valid = true;
        model[index].stamp = index + 1U;
    }
    size_t oldest = 0U;
    for (size_t index = 1U; index < OPENRFSFS_CACHE_ENTRIES; ++index) {
        if (model[index].stamp < model[oldest].stamp) {
            oldest = index;
        }
    }
    if (oldest != 0U) {
        return false;
    }
    ++completed;
    model[oldest].dirty = true;
    if (!model[oldest].dirty || model[oldest].stamp != 1U) {
        return false;
    }
    ++completed;
    openrfsfs_handle encoded = (UINT64_C(7) << 8U) | UINT64_C(3);
    if ((encoded & UINT64_C(0xFF)) != 3U || (encoded >> 8U) != 7U) {
        return false;
    }
    ++completed;
    *completed_tests = completed;
    return completed == 6U;
}

const char *openrfsfs_status_string(enum openrfsfs_status status)
{
    switch (status) {
    case OPENRFSFS_STATUS_OK: return "ok";
    case OPENRFSFS_STATUS_ABSENT: return "volume is absent";
    case OPENRFSFS_STATUS_CORRUPT: return "FAT32 metadata is inconsistent";
    case OPENRFSFS_STATUS_IO: return "storage I/O failed";
    case OPENRFSFS_STATUS_INVALID_ARGUMENT: return "invalid filesystem argument";
    case OPENRFSFS_STATUS_NOT_MOUNTED: return "volume is not mounted";
    case OPENRFSFS_STATUS_ALREADY_MOUNTED: return "volume is already mounted";
    case OPENRFSFS_STATUS_READ_ONLY: return "volume is read-only";
    case OPENRFSFS_STATUS_NOT_FOUND: return "path was not found";
    case OPENRFSFS_STATUS_EXISTS: return "destination already exists";
    case OPENRFSFS_STATUS_NOT_DIRECTORY: return "path component is not a directory";
    case OPENRFSFS_STATUS_IS_DIRECTORY: return "operation needs a regular file";
    case OPENRFSFS_STATUS_NOT_EMPTY: return "directory is not empty";
    case OPENRFSFS_STATUS_BUSY: return "file or volume is busy";
    case OPENRFSFS_STATUS_NO_HANDLES: return "file-handle table is full";
    case OPENRFSFS_STATUS_STALE_HANDLE: return "file handle is stale or closed";
    case OPENRFSFS_STATUS_ACCESS: return "file handle access mode refused the operation";
    case OPENRFSFS_STATUS_RANGE: return "filesystem offset or capacity is out of range";
    case OPENRFSFS_STATUS_FULL: return "volume has no free cluster";
    case OPENRFSFS_STATUS_DIRECTORY_FULL: return "directory has no bounded free slot";
    case OPENRFSFS_STATUS_NAME: return "filename is outside the 8.3 ASCII subset";
    case OPENRFSFS_STATUS_PATH: return "path is malformed or escapes the mount";
    case OPENRFSFS_STATUS_WRITEBACK: return "dirty data could not be written";
    case OPENRFSFS_STATUS_RESET: return "device generation changed";
    default: return "unknown filesystem status";
    }
}
