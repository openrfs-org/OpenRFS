/* SPDX-License-Identifier: GPL-3.0-only */
/* Production terminal writes preserve each backend's truncation boundary. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/kernel/shell.c"

static bool ext4, live, exists, appended;
static unsigned opened_count, closed_count, truncated_count, sync_count;
static enum openrfsfs_status truncate_result, write_result;
static enum openrfsfs_status prepared_result;
static unsigned prepared_count;
static bool create_before_prepared_open;
static char bytes[128];
static size_t length;
static bool interrupts_enabled, queued_keyboard, queued_ui, inject_on_disable;
static unsigned halts;

void cpu_interrupt_disable(void)
{ assert(interrupts_enabled); if (inject_on_disable) queued_keyboard = true; interrupts_enabled = false; }
void cpu_interrupt_enable(void) { assert(!interrupts_enabled); interrupts_enabled = true; }
void cpu_enable_and_halt(void) { assert(!interrupts_enabled); ++halts; interrupts_enabled = true; }
bool keyboard_events_pending(void) { assert(!interrupts_enabled); return queued_keyboard; }
bool ui_events_pending(void) { assert(!interrupts_enabled); return queued_ui; }

bool openrfsfs_has_atomic_replace(enum openrfsfs_volume volume)
{ assert(volume == OPENRFSFS_VOLUME_DATA); return ext4; }
const char *openrfsfs_status_string(enum openrfsfs_status status)
{ (void)status; return "failure"; }
void console_write(const char *text) { (void)text; }
void console_putc(char character) { (void)character; }
enum openrfsfs_status openrfsfs_stat_path(enum openrfsfs_volume volume, const char *path,
    struct openrfsfs_stat *result)
{ (void)result; assert(!ext4 && volume == OPENRFSFS_VOLUME_DATA && strcmp(path, "notes.txt") == 0);
  return exists ? OPENRFSFS_STATUS_OK : OPENRFSFS_STATUS_NOT_FOUND; }
enum openrfsfs_status openrfsfs_create(enum openrfsfs_volume volume, const char *path)
{ (void)volume; (void)path; assert(!ext4 && !exists); exists = true; return OPENRFSFS_STATUS_OK; }
enum openrfsfs_status openrfsfs_open(enum openrfsfs_volume volume, const char *path,
    enum openrfsfs_access access, openrfsfs_handle *handle)
{ (void)volume; (void)path; assert(exists && !live && access == OPENRFSFS_ACCESS_WRITE);
  live = true; appended = false; ++opened_count; *handle = 77U; return OPENRFSFS_STATUS_OK; }
enum openrfsfs_status openrfsfs_open_options(enum openrfsfs_volume volume, const char *path,
    enum openrfsfs_access access, uint8_t flags, uint16_t mode, openrfsfs_handle *handle)
{ assert(ext4 && flags == OPENRFSFS_OPEN_CREATE && mode == UINT16_C(0644));
  ++prepared_count; *handle = 0U;
  if (prepared_result != OPENRFSFS_STATUS_OK) return prepared_result;
  if (create_before_prepared_open) {
      create_before_prepared_open = false; memcpy(bytes, "racing\n", 7U); length = 7U;
  }
  exists = true;
  return openrfsfs_open(volume, path, access, handle); }
static enum openrfsfs_status truncate_bytes(void)
{ ++truncated_count; if (truncate_result == OPENRFSFS_STATUS_OK) length = 0U;
  return truncate_result; }
enum openrfsfs_status openrfsfs_truncate(enum openrfsfs_volume volume, const char *path, uint64_t size)
{ (void)volume; (void)path; assert(!ext4 && !live && size == 0U); return truncate_bytes(); }
enum openrfsfs_status openrfsfs_ftruncate(openrfsfs_handle handle, uint64_t size)
{ assert(ext4 && live && handle == 77U && size == 0U); return truncate_bytes(); }
enum openrfsfs_status openrfsfs_set_append(openrfsfs_handle handle, bool append)
{ assert(live && handle == 77U && append); appended = true; return OPENRFSFS_STATUS_OK; }
enum openrfsfs_status openrfsfs_write(openrfsfs_handle handle, const uint8_t *buffer, size_t count, size_t *written)
{ assert(live && handle == 77U); *written = 0U;
  if (write_result != OPENRFSFS_STATUS_OK) return write_result;
  size_t offset = appended ? length : 0U; assert(offset + count < sizeof(bytes));
  memcpy(bytes + offset, buffer, count); length = offset + count; *written = count;
  return OPENRFSFS_STATUS_OK; }
enum openrfsfs_status openrfsfs_fsync(openrfsfs_handle handle)
{ assert(live && handle == 77U); ++sync_count; return OPENRFSFS_STATUS_OK; }
enum openrfsfs_status openrfsfs_close(openrfsfs_handle handle)
{ assert(live && handle == 77U); live = false; ++closed_count; return OPENRFSFS_STATUS_OK; }

int main(void)
{
    interrupts_enabled = true;
    shell_idle_if_no_input(true);
    assert(halts == 1U && interrupts_enabled);
    inject_on_disable = true; /* Input arrives after the main loop's last drain. */
    shell_idle_if_no_input(true);
    assert(halts == 1U && interrupts_enabled && queued_keyboard);
    inject_on_disable = queued_keyboard = false;
    queued_ui = true; /* Pointer or application event arrived during storage work. */
    shell_idle_if_no_input(true);
    assert(halts == 1U && interrupts_enabled);
    shell_idle_if_no_input(false); /* Disabled UI must not prevent terminal idle. */
    assert(halts == 2U && interrupts_enabled);
    queued_ui = false;
    puts("queued keyboard and application input after storage work prevents lost wakeup: PASS");
    strcpy(filesystem_cwd, ".");
    for (unsigned backend = 0U; backend < 2U; ++backend) {
        ext4 = backend != 0U; exists = false; length = 0U;
        truncate_result = write_result = OPENRFSFS_STATUS_OK;
        opened_count = closed_count = truncated_count = sync_count = 0U;
        command_write_line("notes.txt \"first cut\"", false);
        command_write_line("notes.txt \"second line\"", true);
        assert(length == 22U && memcmp(bytes, "first cut\nsecond line\n", length) == 0);
        assert(!live && opened_count == 2U && closed_count == 2U && truncated_count == 1U && sync_count == 2U);
        command_write_line("notes.txt \"short\"", false);
        assert(length == 6U && memcmp(bytes, "short\n", length) == 0);
        truncate_result = OPENRFSFS_STATUS_IO;
        const unsigned before = opened_count;
        command_write_line("notes.txt \"refused\"", false);
        assert(!live && length == 6U && opened_count == before + (ext4 ? 1U : 0U));
        assert(opened_count == closed_count && sync_count == 3U);
        truncate_result = OPENRFSFS_STATUS_OK; write_result = OPENRFSFS_STATUS_IO;
        command_write_line("notes.txt \"refused\"", true);
        assert(!live && length == 6U && opened_count == closed_count && sync_count == 3U);
    }
    const unsigned before_prepared_failure = opened_count;
    const unsigned before_truncate_failure = truncated_count;
    prepared_result = OPENRFSFS_STATUS_NO_HANDLES;
    exists = false; length = 0U;
    command_write_line("notes.txt \"no handle\"", true);
    assert(!exists && !live && opened_count == before_prepared_failure &&
        truncated_count == before_truncate_failure && sync_count == 3U);
    exists = true; memcpy(bytes, "retained", 8U); length = 8U;
    command_write_line("notes.txt \"no handle\"", false);
    assert(!live && length == 8U && memcmp(bytes, "retained", 8U) == 0 &&
        opened_count == before_prepared_failure && truncated_count == before_truncate_failure);
    prepared_result = write_result = OPENRFSFS_STATUS_OK;
    exists = false; length = 0U; create_before_prepared_open = true;
    command_write_line("notes.txt \"tail\"", true);
    assert(!live && !create_before_prepared_open && prepared_count == 8U &&
        length == 12U && memcmp(bytes, "racing\ntail\n", 12U) == 0 &&
        opened_count == closed_count && sync_count == 4U);
    puts("terminal ext4/FAT32 write, append, truncate and failure cleanup: PASS");
    return 0;
}
