/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The registry of TPMs driven by upstream drivers. See include/openrfs/tpm.h.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/tpm.h>

struct tpm_device {
    char name[TPM_NAME_CAPACITY];
    char driver[TPM_DRIVER_CAPACITY];
    char description[TPM_DESCRIPTION_CAPACITY];
    uint32_t version;
    const struct tpm_operations *operations;
    void *context;
};

static struct tpm_device devices[TPM_MAX_DEVICES];
static size_t device_count;

static const char *const status_strings[TPM_STATUS_COUNT] = {
    "ok",
    "null argument",
    "no such TPM",
    "TPM table full",
    "malformed TPM command",
    "the TPM interface reported an error",
    "TPM busy"
};

static void copy_text(char *destination, const char *source, size_t capacity)
{
    size_t index = 0U;

    while (source != NULL && index + 1U < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

static uint32_t big_endian_32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
        ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

enum tpm_status tpm_register(const char *driver, const char *description,
    uint32_t version, const struct tpm_operations *operations, void *context,
    char name[TPM_NAME_CAPACITY])
{
    struct tpm_device *device;

    if (driver == NULL || description == NULL || operations == NULL ||
        operations->transmit == NULL || name == NULL) {
        return TPM_STATUS_NULL_ARGUMENT;
    }
    if (device_count >= TPM_MAX_DEVICES) {
        return TPM_STATUS_TABLE_FULL;
    }
    device = &devices[device_count];
    device->name[0] = 't';
    device->name[1] = 'p';
    device->name[2] = 'm';
    device->name[3] = (char)('0' + (char)device_count);
    device->name[4] = '\0';
    copy_text(device->driver, driver, sizeof(device->driver));
    copy_text(device->description, description, sizeof(device->description));
    device->version = version;
    device->operations = operations;
    device->context = context;
    ++device_count;
    copy_text(name, device->name, TPM_NAME_CAPACITY);
    return TPM_STATUS_OK;
}

size_t tpm_count(void)
{
    return device_count;
}

bool tpm_info(size_t index, struct tpm_info *info)
{
    if (info == NULL || index >= device_count) {
        return false;
    }
    copy_text(info->name, devices[index].name, sizeof(info->name));
    copy_text(info->driver, devices[index].driver, sizeof(info->driver));
    copy_text(info->description, devices[index].description,
        sizeof(info->description));
    info->version = devices[index].version;
    return true;
}

enum tpm_status tpm_transmit(size_t index, const void *command,
    uint32_t command_length, void *response, uint32_t response_capacity,
    uint32_t *response_length)
{
    uint32_t length = response_capacity;
    enum tpm_status status;

    if (command == NULL || response == NULL || response_length == NULL) {
        return TPM_STATUS_NULL_ARGUMENT;
    }
    if (index >= device_count) {
        return TPM_STATUS_ABSENT;
    }
    /* The interface sends as many bytes as the header says, no more. */
    if (command_length < TPM_HEADER_BYTES ||
        command_length > TPM_MAX_COMMAND_BYTES ||
        big_endian_32((const uint8_t *)command + 2U) != command_length ||
        response_capacity < TPM_HEADER_BYTES) {
        return TPM_STATUS_BAD_COMMAND;
    }
    status = devices[index].operations->transmit(devices[index].context,
        command, response, &length);
    if (status != TPM_STATUS_OK) {
        return status;
    }
    if (length < TPM_HEADER_BYTES || length > response_capacity ||
        big_endian_32((const uint8_t *)response + 2U) != length) {
        return TPM_STATUS_DEVICE_ERROR;
    }
    *response_length = length;
    return TPM_STATUS_OK;
}

const char *tpm_status_string(enum tpm_status status)
{
    if ((unsigned int)status >= TPM_STATUS_COUNT) {
        return "unknown TPM status";
    }
    return status_strings[status];
}
