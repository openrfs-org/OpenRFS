/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OPENRFS_TPM_H
#define OPENRFS_TPM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Trusted Platform Modules driven by upstream drivers. A TPM is a command
 * processor: the kernel hands it one marshalled command and reads back one
 * response. Nothing here interprets either; callers speak TPM 2.0 (or 1.2)
 * themselves.
 */
#define TPM_MAX_DEVICES 1U
#define TPM_NAME_CAPACITY 8U
#define TPM_DRIVER_CAPACITY 24U
#define TPM_DESCRIPTION_CAPACITY 64U
/* The PC Client TIS FIFO and CRB buffers hold at most 4 KiB. */
#define TPM_MAX_COMMAND_BYTES 4096U
#define TPM_HEADER_BYTES 10U

enum tpm_status {
    TPM_STATUS_OK = 0,
    TPM_STATUS_NULL_ARGUMENT,
    TPM_STATUS_ABSENT,
    TPM_STATUS_TABLE_FULL,
    TPM_STATUS_BAD_COMMAND,
    TPM_STATUS_DEVICE_ERROR,
    TPM_STATUS_BUSY,
    TPM_STATUS_COUNT
};

struct tpm_operations {
    /* response_length: in, the buffer's size; out, the response's. */
    enum tpm_status (*transmit)(void *context, const void *command,
        void *response, uint32_t *response_length);
};

struct tpm_info {
    char name[TPM_NAME_CAPACITY];
    char driver[TPM_DRIVER_CAPACITY];
    char description[TPM_DESCRIPTION_CAPACITY];
    uint32_t version;   /* 1 for TPM 1.2, 2 for TPM 2.0 */
};

enum tpm_status tpm_register(const char *driver, const char *description,
    uint32_t version, const struct tpm_operations *operations, void *context,
    char name[TPM_NAME_CAPACITY]);
size_t tpm_count(void);
bool tpm_info(size_t index, struct tpm_info *info);
/*
 * Send one command. Its header's size field must equal command_length and
 * lie between the header and TPM_MAX_COMMAND_BYTES; the response must fit
 * the buffer and carry at least a response header.
 */
enum tpm_status tpm_transmit(size_t index, const void *command,
    uint32_t command_length, void *response, uint32_t response_capacity,
    uint32_t *response_length);
const char *tpm_status_string(enum tpm_status status);

#endif
