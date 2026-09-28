/* SPDX-License-Identifier: GPL-3.0-only */
/* Linux host adapter for the production Multiboot2 information parser. */
#define _GNU_SOURCE
#include <openrfs/boot.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#ifdef OPENRFS_REPLAY
#include <stdio.h>
#endif

#define MAX_INPUT_BYTES 4097U

static void require(bool condition)
{
    if (!condition) {
        abort();
    }
}

static bool inside(const struct boot_information *context,
    const void *pointer, size_t bytes)
{
    const uintptr_t start = (uintptr_t)pointer;

    return start >= context->information_start &&
        start <= context->information_end &&
        bytes <= context->information_end - start;
}

static enum boot_status run_input(const uint8_t *input, size_t size)
{
    struct boot_information context = { 0 };
    uint8_t *physical;
    size_t count;
    uint32_t magic;
    enum boot_status status;

    if (size > MAX_INPUT_BYTES) {
        return BOOT_STATUS_INFORMATION_TOO_LARGE;
    }
    count = size == 0U ? 0U : size - 1U;
    physical = mmap(NULL, MULTIBOOT2_MAX_INFORMATION_SIZE,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT,
        -1, 0);
    require(physical != MAP_FAILED);
    if (count != 0U) {
        memcpy(physical, input + 1U, count);
    }
    /* Byte zero switches only the boot magic; all other bytes are passed
     * unchanged to the production parser. The rest of the mapped physical
     * window is zeroed by mmap, as it would be reachable early boot memory. */
    magic = size != 0U && (input[0] & 1U) != 0U
        ? 0U : MULTIBOOT2_BOOT_MAGIC;
    status = boot_information_parse(magic, (uintptr_t)physical, &context);
    require(boot_status_string(status) != NULL);
    if (status == BOOT_STATUS_OK) {
        require(context.information_start == (uintptr_t)physical);
        require(context.information_end > context.information_start);
        require(context.information_end - context.information_start <=
            MULTIBOOT2_MAX_INFORMATION_SIZE);
        require(context.memory_map != NULL &&
            inside(&context, context.memory_map, sizeof(*context.memory_map)));
        if (context.memory_map_entry_count != 0U) {
            struct boot_memory_region region;

            require(boot_information_region_at(&context, 0U, &region));
            require(boot_information_region_at(&context,
                context.memory_map_entry_count - 1U, &region));
            require(!boot_information_region_at(&context,
                context.memory_map_entry_count, &region));
        }
        if (context.command_line != NULL) {
            require(inside(&context, context.command_line,
                context.command_line_length + 1U));
        }
        if (context.boot_loader_name != NULL) {
            require(inside(&context, context.boot_loader_name,
                context.boot_loader_name_length + 1U));
        }
        if (context.acpi_old != NULL) {
            require(inside(&context, context.acpi_old,
                sizeof(*context.acpi_old) + 20U));
        }
        if (context.acpi_new != NULL) {
            require(inside(&context, context.acpi_new,
                sizeof(*context.acpi_new) + 36U));
        }
        if (context.framebuffer.present) {
            require(context.framebuffer.size != 0U);
            require(context.framebuffer.address <=
                OPENRFS_EARLY_PHYSICAL_LIMIT - context.framebuffer.size);
        }
    }

    /* Reuse the same context to detect stale pointers and framebuffer state. */
    require(boot_information_parse(0U, (uintptr_t)physical, &context) ==
        BOOT_STATUS_BAD_MAGIC);
    require(context.memory_map == NULL && context.acpi_old == NULL &&
        context.acpi_new == NULL && context.command_line == NULL &&
        context.boot_loader_name == NULL && !context.framebuffer.present &&
        context.memory_map_entry_count == 0U &&
        context.reported_usable_bytes == 0U &&
        context.highest_reported_address == 0U);
    require(munmap(physical, MULTIBOOT2_MAX_INFORMATION_SIZE) == 0);
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
    enum boot_status status;

    if (argc != 2 || (stream = fopen(argv[1], "rb")) == NULL) {
        fprintf(stderr, "usage: multiboot2-replay INPUT\n");
        return 2;
    }
    count = fread(input, 1U, sizeof(input), stream);
    if (ferror(stream) || fclose(stream) != 0) {
        fprintf(stderr, "cannot read input\n");
        return 2;
    }
    status = run_input(input, count);
    printf("status=%d accepted=%d\n", (int)status,
        status == BOOT_STATUS_OK ? 1 : 0);
    return 0;
}
#endif
