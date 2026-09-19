/* SPDX-License-Identifier: GPL-3.0-only */
/* Competing public mount calls must not overlap backend lifecycle callbacks. */
#include <assert.h>
#include <stdio.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif
#include "../src/kernel/vfs.c"

#define WORKERS 16U
static _Thread_local bool host_interrupts_enabled = true;
bool cpu_interrupts_enabled(void) { return host_interrupts_enabled; }
void cpu_interrupt_disable(void) { host_interrupts_enabled = false; }
void cpu_interrupt_enable(void) { host_interrupts_enabled = true; }
static unsigned phase, completed, backend_calls;
static bool backend_entered, release_backend;
static bool lookup_fails;
static bool observer_done;
static unsigned observations;
static enum openrfsfs_status results[WORKERS];

static void yield_worker(void)
{
#ifdef _WIN32
    (void)SwitchToThread();
#else
    (void)sched_yield();
#endif
}

static enum openrfsfs_status backend_transition(enum openrfsfs_volume volume)
{
    assert(volume == OPENRFSFS_VOLUME_DATA && cpu_interrupts_enabled());
    __atomic_fetch_add(&backend_calls, 1U, __ATOMIC_RELAXED);
    const struct vfs_backend_ops *pinned = NULL;
    assert(mount_pin(volume, &pinned) == OPENRFSFS_STATUS_NOT_MOUNTED && pinned == NULL);
    assert(openrfsfs_mount(volume) == OPENRFSFS_STATUS_BUSY && openrfsfs_unmount(volume) == OPENRFSFS_STATUS_BUSY);
    assert(!openrfsfs_resources_released());
    __atomic_store_n(&backend_entered, true, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&release_backend, __ATOMIC_ACQUIRE)) yield_worker();
    return phase % 2U == 0U ? OPENRFSFS_STATUS_IO : OPENRFSFS_STATUS_OK;
}

static struct openrfsfs_drive_info backend_drive(enum openrfsfs_volume volume)
{
    assert(volume == OPENRFSFS_VOLUME_DATA && cpu_interrupts_enabled());
    (void)openrfsfs_has_atomic_replace(volume);
    return (struct openrfsfs_drive_info){ .mounted = true };
}

static uint64_t backend_completion_count(enum openrfsfs_volume volume)
{
    assert(volume == OPENRFSFS_VOLUME_DATA && cpu_interrupts_enabled());
    (void)openrfsfs_has_atomic_replace(volume);
    return 77U;
}

static enum openrfsfs_status backend_replace(enum openrfsfs_volume volume, const char *from, const char *to)
{
    (void)volume; (void)from; (void)to;
    assert(false);
    return OPENRFSFS_STATUS_ACCESS;
}

static void observe_mount(void)
{
    do {
        assert(openrfsfs_drive(OPENRFSFS_VOLUME_DATA).mounted);
        assert(openrfsfs_completion_count(OPENRFSFS_VOLUME_DATA) == 77U);
        (void)openrfsfs_has_atomic_replace(OPENRFSFS_VOLUME_DATA);
        (void)openrfsfs_resources_released();
        assert(cpu_interrupts_enabled());
        __atomic_fetch_add(&observations, 1U, __ATOMIC_RELEASE);
    } while (!__atomic_load_n(&observer_done, __ATOMIC_ACQUIRE));
}

static enum openrfsfs_status backend_sync(enum openrfsfs_volume volume)
{
    assert(cpu_interrupts_enabled() && openrfsfs_unmount(volume) == OPENRFSFS_STATUS_BUSY);
    return OPENRFSFS_STATUS_IO;
}

static enum openrfsfs_status backend_stat(enum openrfsfs_volume volume, const char *path, struct openrfsfs_stat *stat)
{
    assert(cpu_interrupts_enabled() && text_equal(path, "held"));
    assert(openrfsfs_unmount(volume) == OPENRFSFS_STATUS_BUSY);
    if (lookup_fails) return OPENRFSFS_STATUS_IO;
    *stat = (struct openrfsfs_stat){ .object_id = 500U, .size = 1700U };
    return OPENRFSFS_STATUS_OK;
}

static void exercise_mount(size_t worker)
{
    results[worker] = phase < 2U ? openrfsfs_mount(OPENRFSFS_VOLUME_DATA) : openrfsfs_unmount(OPENRFSFS_VOLUME_DATA);
    assert(cpu_interrupts_enabled());
    __atomic_fetch_add(&completed, 1U, __ATOMIC_RELEASE);
}

#ifdef _WIN32
static DWORD WINAPI observer_main(LPVOID argument)
{
    (void)argument;
    observe_mount();
    return 0U;
}

static DWORD WINAPI worker_main(LPVOID argument)
{
    exercise_mount((size_t)(uintptr_t)argument);
    return 0U;
}
#else
static void *observer_main(void *argument)
{
    (void)argument;
    observe_mount();
    return NULL;
}

static void *worker_main(void *argument)
{
    exercise_mount((size_t)(uintptr_t)argument);
    return NULL;
}
#endif

int main(void)
{
    static const struct vfs_backend_ops backend = {
        .mount = backend_transition, .unmount = backend_transition, .drive = backend_drive, .sync = backend_sync,
        .completion_count = backend_completion_count, .rename_replace = backend_replace,
        .stat_path = backend_stat, .validates_mutation_paths = true, .case_sensitive = true };
    volume_backends[OPENRFSFS_VOLUME_DATA] = &backend;
    for (size_t index = 0U; index < VFS_VNODE_BUCKETS; ++index) vnode_buckets[index] = VFS_NO_INDEX;
    const uint64_t generation_before = next_mount_generation;
    for (phase = 0U; phase < 4U; ++phase) {
        completed = backend_calls = 0U;
        backend_entered = release_backend = false;
        observer_done = false;
        observations = 0U;
#ifdef _WIN32
        HANDLE workers[WORKERS];
        HANDLE observer = CreateThread(NULL, 0U, observer_main, NULL, 0U, NULL);
        assert(observer != NULL);
#else
        pthread_t workers[WORKERS];
        pthread_t observer;
        assert(pthread_create(&observer, NULL, observer_main, NULL) == 0);
#endif
        for (size_t index = 0U; index < WORKERS; ++index) {
#ifdef _WIN32
            workers[index] = CreateThread(NULL, 0U, worker_main, (void *)(uintptr_t)index, 0U, NULL);
            assert(workers[index] != NULL);
#else
            assert(pthread_create(&workers[index], NULL, worker_main, (void *)(uintptr_t)index) == 0);
#endif
        }
        while (!__atomic_load_n(&backend_entered, __ATOMIC_ACQUIRE) ||
                __atomic_load_n(&completed, __ATOMIC_ACQUIRE) != WORKERS - 1U) yield_worker();
        assert(__atomic_load_n(&backend_calls, __ATOMIC_RELAXED) == 1U);
        assert(!openrfsfs_has_atomic_replace(OPENRFSFS_VOLUME_DATA));
        while (__atomic_load_n(&observations, __ATOMIC_ACQUIRE) < 100U) yield_worker();
        __atomic_store_n(&release_backend, true, __ATOMIC_RELEASE);
        for (size_t index = 0U; index < WORKERS; ++index) {
#ifdef _WIN32
            assert(WaitForSingleObject(workers[index], INFINITE) == WAIT_OBJECT_0);
            assert(CloseHandle(workers[index]));
#else
            assert(pthread_join(workers[index], NULL) == 0);
#endif
        }
        __atomic_store_n(&observer_done, true, __ATOMIC_RELEASE);
#ifdef _WIN32
        assert(WaitForSingleObject(observer, INFINITE) == WAIT_OBJECT_0);
        assert(CloseHandle(observer));
#else
        assert(pthread_join(observer, NULL) == 0);
#endif
        unsigned owned = 0U;
        for (size_t index = 0U; index < WORKERS; ++index) {
            if (results[index] == OPENRFSFS_STATUS_BUSY) continue;
            assert(results[index] == (phase % 2U == 0U ? OPENRFSFS_STATUS_IO : OPENRFSFS_STATUS_OK));
            ++owned;
        }
        assert(owned == 1U && !mounts[OPENRFSFS_VOLUME_DATA].mounting && !mounts[OPENRFSFS_VOLUME_DATA].unmounting);
        assert(mounts[OPENRFSFS_VOLUME_DATA].active == (phase == 1U || phase == 2U));
        assert(openrfsfs_has_atomic_replace(OPENRFSFS_VOLUME_DATA) == (phase == 1U || phase == 2U));
        assert(next_mount_generation == generation_before + (phase == 0U ? 0U : 1U));
        if (phase == 1U || phase == 2U) {
            assert(mounts[OPENRFSFS_VOLUME_DATA].generation == generation_before);
            const struct vfs_backend_ops *pinned;
            assert(mount_pin(OPENRFSFS_VOLUME_DATA, &pinned) == OPENRFSFS_STATUS_OK && pinned == &backend);
            assert(openrfsfs_unmount(OPENRFSFS_VOLUME_DATA) == OPENRFSFS_STATUS_BUSY);
            mount_release(OPENRFSFS_VOLUME_DATA);
            assert(openrfsfs_sync(OPENRFSFS_VOLUME_DATA) == OPENRFSFS_STATUS_IO);
            assert(mounts[OPENRFSFS_VOLUME_DATA].references == 0U);
            struct openrfsfs_stat stat;
            lookup_fails = false;
            assert(openrfsfs_stat_path(OPENRFSFS_VOLUME_DATA, "held", &stat) == OPENRFSFS_STATUS_OK);
            assert(stat.object_id == 500U && stat.size == 1700U);
            lookup_fails = true;
            assert(openrfsfs_stat_path(OPENRFSFS_VOLUME_DATA, "held", &stat) == OPENRFSFS_STATUS_IO);
            assert(stat.object_id == 0U && stat.size == 0U);
            assert(mounts[OPENRFSFS_VOLUME_DATA].references == 0U && vnode_resources_released());
        }
    }
    assert(openrfsfs_resources_released());
    const size_t file_slot = openrfs_slot_claim(open_file_claims, VFS_MAX_OPEN_FILES);
    assert(file_slot < VFS_MAX_OPEN_FILES && !openrfsfs_resources_released());
    openrfs_slot_release(open_file_claims, file_slot);
    const size_t directory_slot = openrfs_slot_claim(directory_claims, VFS_MAX_DIRECTORY_ITERATORS);
    assert(directory_slot < VFS_MAX_DIRECTORY_ITERATORS && !openrfsfs_resources_released());
    openrfs_slot_release(directory_claims, directory_slot);
    const size_t reserved = vnode_reserve();
    assert(reserved < VFS_MAX_VNODES && !openrfsfs_resources_released());
    vnode_unreserve(reserved);
    assert(openrfsfs_resources_released() && cpu_interrupts_enabled());
    assert(!openrfsfs_drive(OPENRFSFS_VOLUME_COUNT).mounted);
    assert(openrfsfs_completion_count(OPENRFSFS_VOLUME_COUNT) == 0U);
    assert(!openrfsfs_has_atomic_replace(OPENRFSFS_VOLUME_COUNT));
    puts("VFS concurrent mount/unmount: one owner, rollback, generations, concurrent observers and reentrant callbacks PASS");
    return 0;
}
