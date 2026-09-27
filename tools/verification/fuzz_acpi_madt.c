/* SPDX-License-Identifier: GPL-3.0-only */
/* Linux host adapter for the production early-boot MADT topology parser. */
#define _GNU_SOURCE
#include <openrfs/acpi.h>
#include <openrfs/acpi_util.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#ifdef OPENRFS_REPLAY
#include <stdio.h>
#endif

#define MAX_INPUT_BYTES 2049U
#define MADT_FIXED_BYTES 44U

static void require(bool condition)
{
    if (!condition) {
        abort();
    }
}

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8U |
        (uint32_t)bytes[2] << 16U | (uint32_t)bytes[3] << 24U;
}

static enum acpi_status run_input(const uint8_t *input, size_t size)
{
    uint8_t *table;
    size_t count;
    struct acpi_madt madt = { 0 };
    struct acpi_topology topology;
    enum acpi_status status;

    if (size < 1U || size > MAX_INPUT_BYTES) {
        return ACPI_STATUS_BAD_MADT_LENGTH;
    }
    count = size - 1U;
    table = mmap(NULL, 4096U, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    require(table != MAP_FAILED);
    memcpy(table, input + 1U, count);
    if (input[0] == 1U && count >= MADT_FIXED_BYTES) {
        /* Reach entry parsing even after mutations of a checksummed table. */
        table[9] = 0U;
        table[9] = (uint8_t)(0U - acpi_byte_sum(table, count));
    }
    madt.physical_address = (uint64_t)(uintptr_t)table;
    madt.length = (uint32_t)count;
    if (count >= MADT_FIXED_BYTES) {
        madt.local_apic_address = read_u32(table + 36U);
        madt.flags = read_u32(table + 40U);
    }
    status = acpi_topology_discover(&madt, &topology);
    if (status == ACPI_STATUS_OK) {
        require(topology.enabled_processor_count > 0U);
        require(topology.local_apic_count >= topology.enabled_processor_count);
        require(topology.io_apic_count > 0U);
        require(topology.local_apic_count <= ACPI_MAX_LOCAL_APICS);
        require(topology.io_apic_count <= ACPI_MAX_IO_APICS);
    } else {
        const uint8_t *bytes = (const uint8_t *)&topology;
        for (size_t index = 0U; index < sizeof(topology); ++index) {
            require(bytes[index] == 0U);
        }
    }
    require(munmap(table, 4096U) == 0);
    return status;
}

int LLVMFuzzerTestOneInput(const uint8_t *input, size_t size)
{
    (void)run_input(input, size);
    return 0;
}

#ifdef OPENRFS_REPLAY
int main(int argc, char **argv)
{
    uint8_t input[MAX_INPUT_BYTES + 1U];
    size_t count;
    FILE *stream;
    enum acpi_status status;

    if (argc != 2 || (stream = fopen(argv[1], "rb")) == NULL) {
        fprintf(stderr, "usage: acpi-madt-replay INPUT\n");
        return 2;
    }
    count = fread(input, 1U, sizeof(input), stream);
    if (ferror(stream) || fclose(stream) != 0) {
        fprintf(stderr, "cannot read input\n");
        return 2;
    }
    status = run_input(input, count);
    printf("status=%d accepted=%d\n", (int)status,
        status == ACPI_STATUS_OK ? 1 : 0);
    return 0;
}
#endif
