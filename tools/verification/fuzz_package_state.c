/* SPDX-License-Identifier: GPL-3.0-only */
/* Host-only libFuzzer and one-file replay of the production package parser. */
#include <openrfs/package_state.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef OPENRFS_REPLAY
#include <stdio.h>
#endif

#define MAX_INPUT_BYTES 8193U

static void require(bool condition)
{
    if (!condition) {
        abort();
    }
}

static enum package_state_status check_database(
    const uint8_t *bytes, size_t count)
{
    struct package_state_database_view database = { 0 };
    enum package_state_status status = package_state_database_parse(
        bytes, count, &database);
    if (status != PACKAGE_STATE_STATUS_OK) {
        return status;
    }
    require(database.bytes == bytes && database.byte_count == count);
    require(database.generation != 0U);
    require(database.package_count <= PACKAGE_STATE_DATABASE_MAX_PACKAGES);
    require(database.edge_count <= PACKAGE_STATE_DATABASE_MAX_EDGES);
    require(database.file_count <= PACKAGE_STATE_DATABASE_MAX_FILES);
    for (uint32_t index = 0U; index < database.package_count; ++index) {
        struct package_state_package_view package = { 0 };
        require(package_state_database_package(&database, index, &package) ==
            PACKAGE_STATE_STATUS_OK);
        require(package.database == &database && package.package_index == index);
    }
    for (uint32_t index = 0U; index < database.edge_count; ++index) {
        struct package_state_dependency_view dependency = { 0 };
        require(package_state_database_dependency(&database, index, &dependency) ==
            PACKAGE_STATE_STATUS_OK);
        require(dependency.database == &database &&
            dependency.dependency_index == index);
    }
    for (uint32_t index = 0U; index < database.file_count; ++index) {
        struct package_state_file_view file = { 0 };
        require(package_state_database_file(&database, index, &file) ==
            PACKAGE_STATE_STATUS_OK);
        require(file.database == &database && file.file_index == index);
    }
    uint8_t authority[PACKAGE_STATE_AUTHORITY_BYTES];
    struct package_state_authority_view admitted = { 0 };
    require(package_state_authority_encode(&database, authority) ==
        PACKAGE_STATE_STATUS_OK);
    require(package_state_authority_parse(authority, sizeof(authority), &admitted) ==
        PACKAGE_STATE_STATUS_OK);
    require(admitted.generation == database.generation &&
        admitted.database_bytes == count);
    return status;
}

static enum package_state_status check_authority(
    const uint8_t *bytes, size_t count)
{
    struct package_state_authority_view authority = { 0 };
    enum package_state_status status = package_state_authority_parse(
        bytes, count, &authority);
    if (status == PACKAGE_STATE_STATUS_OK) {
        require(authority.generation != 0U);
        require(authority.database_bytes >= PACKAGE_STATE_DATABASE_HEADER_BYTES);
        require(authority.database_bytes <= PACKAGE_STATE_DATABASE_MAX_BYTES);
    }
    return status;
}

static enum package_state_status check_journal(
    const uint8_t *bytes, size_t count)
{
    struct package_state_journal_view journal = { 0 };
    enum package_state_status status = package_state_journal_parse(
        bytes, count, &journal);
    if (status == PACKAGE_STATE_STATUS_OK) {
        require(journal.operation >= PACKAGE_STATE_OPERATION_INSTALL &&
            journal.operation <= PACKAGE_STATE_OPERATION_REPAIR);
        require(journal.base_generation != 0U &&
            journal.target_generation == journal.base_generation + 1U);
        require(journal.required_space != 0U);
    }
    return status;
}

static enum package_state_status run_input(
    const uint8_t *input, size_t size)
{
    if (size < 1U || size > MAX_INPUT_BYTES) {
        return PACKAGE_STATE_STATUS_LENGTH;
    }
    switch (input[0]) {
        case 0U: return check_database(input + 1U, size - 1U);
        case 1U: return check_authority(input + 1U, size - 1U);
        case 2U: return check_journal(input + 1U, size - 1U);
        default: return PACKAGE_STATE_STATUS_HEADER;
    }
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
    enum package_state_status status;

    if (argc != 2 || (stream = fopen(argv[1], "rb")) == NULL) {
        fprintf(stderr, "usage: package-state-replay INPUT\n");
        return 2;
    }
    count = fread(input, 1U, sizeof(input), stream);
    if (ferror(stream) || fclose(stream) != 0) {
        fprintf(stderr, "cannot read input\n");
        return 2;
    }
    status = run_input(input, count);
    printf("status=%d accepted=%d\n", (int)status,
        status == PACKAGE_STATE_STATUS_OK ? 1 : 0);
    return 0;
}
#endif
