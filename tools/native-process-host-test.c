/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/kernel/native_process.c"

int main(void)
{
    bool complete;
    char path[OPENRFSFS_MAX_PATH];
    struct native_pipe *pipe;
    uint64_t pipe_id;

    assert(data_path_from_cwd("", "notes", 5U, path) &&
        strcmp(path, "notes") == 0);
    assert(data_path_from_cwd("one/two", "../three/./item", 15U, path) &&
        strcmp(path, "one/three/item") == 0);
    assert(data_path_from_cwd("one", "../../four", 10U, path) &&
        strcmp(path, "four") == 0);
    assert(data_path_from_cwd("one", "..", 2U, path) &&
        strcmp(path, ".") == 0);
    assert(!data_path_from_cwd("one", "bad:name", 8U, path));
    assert(!data_path_from_cwd("one", "bad\\name", 8U, path));

    zero_bytes(pipes, sizeof(pipes));
    next_pipe_id = 1U;
    pipe = pipe_allocate();
    assert(pipe != NULL);
    pipe_id = pipe->id;
    assert(pipe_retain(pipe_id, NATIVE_PIPE_READER));
    assert(pipe_retain(pipe_id, NATIVE_PIPE_WRITER));
    assert(!pipe_transfer_ready(pipe, NATIVE_PIPE_READER, 1U));
    assert(pipe_transfer_ready(pipe, NATIVE_PIPE_WRITER, 4096U));
    pipe->count = 4095U;
    assert(!pipe_transfer_ready(pipe, NATIVE_PIPE_WRITER, 2U));
    assert(pipe_transfer_ready(pipe, NATIVE_PIPE_WRITER, 1U));
    assert(pipe_release(pipe_id, NATIVE_PIPE_READER));
    assert(pipe_transfer_ready(pipe, NATIVE_PIPE_WRITER, 4096U));
    assert(pipe_release(pipe_id, NATIVE_PIPE_WRITER));
    assert(pipe_by_id(pipe_id) == NULL);
    pipe = pipe_allocate();
    assert(pipe != NULL && pipe->id != pipe_id);
    zero_bytes(pipes, sizeof(pipes));

    next_process_generation = INT32_MAX;
    assert(claim_process_id() == INT32_MAX);
    assert(claim_process_id() == 0U);
    next_process_generation = 3U;

    zero_bytes(processes, sizeof(processes));
    processes[0].generation = 1U;
    processes[0].active = true;
    processes[0].session_id = 1U;
    processes[0].process_group = 1U;
    processes[0].file_creation_mask = 0022U;
    assert(syscall_process_umask(&processes[0], 0077U) == 0022U);
    assert(processes[0].file_creation_mask == 0077U);
    assert(syscall_process_umask(&processes[0], 01722U) == 0077U);
    assert(processes[0].file_creation_mask == 0722U);
    assert(syscall_process_umask(&processes[0], 0022U) == 0722U);
    processes[1].generation = 2U;
    processes[1].parent_generation = 1U;
    processes[1].active = true;
    processes[1].session_id = 1U;
    processes[1].process_group = 1U;
    assert(process_wait_child(&processes[0], 2, 0U, 0U, true,
        &complete) == 0 && complete);
    assert(process_wait_child(&processes[0], 3, 0U, 0U, false,
        &complete) == -OPENRFS_ECHILD && complete);
    assert(process_wait_child(&processes[0], 2, 0U, 0U, false,
        &complete) == 0 && !complete);
    processes[1].active = false;
    processes[1].zombie = true;
    processes[1].exit_status = 23;
    assert(process_wait_child(&processes[0], -1, 0U, 0U, false,
        &complete) == 2 && complete);
    assert(processes[1].generation == 0U);
    assert(process_wait_child(&processes[0], -1, 0U, 0U, false,
        &complete) == -OPENRFS_ECHILD && complete);

    processes[1].generation = 3U;
    processes[1].parent_generation = 1U;
    processes[1].active = true;
    processes[2].generation = 4U;
    processes[2].parent_generation = 9U;
    processes[2].active = true;
    assert(syscall_process_disposition(&processes[1], 15U, 1U) == 0);
    assert(syscall_process_disposition(&processes[1], 15U, 1U) == 1);
    assert(syscall_process_signal(&processes[0], 3, 15) == 0);
    assert(!processes[1].exiting);
    assert(syscall_process_disposition(&processes[1], 15U, 0U) == 1);
    assert(syscall_process_disposition(&processes[1], 9U, 1U) ==
        -OPENRFS_EINVAL);
    assert(syscall_process_disposition(&processes[1], 17U, 1U) == 0);
    assert(syscall_process_disposition(&processes[1], 17U, 0U) == 1);
    assert(syscall_process_signal(&processes[0], 4, 0) == -OPENRFS_EPERM);
    assert(syscall_process_signal(&processes[0], 5, 0) == -OPENRFS_ESRCH);
    assert(syscall_process_signal(&processes[0], 0, 0) == 0);
    assert(syscall_process_signal(&processes[0], INT32_MAX + INT64_C(1), 0) ==
        -OPENRFS_ESRCH);
    assert(syscall_process_signal(&processes[0], 3, 65) == -OPENRFS_EINVAL);
    assert(syscall_process_signal(&processes[0], 3, 0) == 0);
    assert(syscall_process_signal(&processes[0], 3, 15) == 0);
    assert(processes[1].exiting && processes[1].termination_signal == 15U &&
        processes[1].exit_status == 143);
    assert(syscall_process_signal(&processes[0], 3, 0) == -OPENRFS_ESRCH);
    zero_bytes(&processes[1], sizeof(processes[1]));
    zero_bytes(&processes[2], sizeof(processes[2]));

    processes[1].generation = 3U;
    processes[1].parent_generation = 1U;
    processes[1].session_id = 1U;
    processes[1].process_group = 1U;
    processes[1].active = true;
    processes[2].generation = 4U;
    processes[2].parent_generation = 1U;
    processes[2].session_id = 1U;
    processes[2].process_group = 1U;
    processes[2].active = true;
    processes[3].generation = 5U;
    processes[3].parent_generation = 1U;
    processes[3].session_id = 5U;
    processes[3].process_group = 5U;
    processes[3].active = true;
    assert(syscall_process_group_get(&processes[0], 0) == 1);
    assert(syscall_process_group_get(&processes[0], 5) == -OPENRFS_EPERM);
    assert(syscall_process_group_set(&processes[0], 5, 0) == -OPENRFS_EPERM);
    assert(syscall_process_signal(&processes[0], -5, 0) == -OPENRFS_ESRCH);
    assert(syscall_process_group_set(&processes[0], 0, 0) == -OPENRFS_EPERM);
    processes[1].has_executed = true;
    assert(syscall_process_group_set(&processes[0], 3, 0) == -OPENRFS_EACCES);
    processes[1].has_executed = false;
    assert(syscall_process_group_set(&processes[0], 3, 4) == -OPENRFS_EPERM);
    assert(syscall_process_group_set(&processes[0], 3, 0) == 0);
    assert(syscall_process_group_set(&processes[0], 4, 3) == 0);
    assert(syscall_process_group_get(&processes[0], 4) == 3);
    assert(process_wait_child(&processes[0], 0, 1U, 0U, true,
        &complete) == -OPENRFS_ECHILD && complete);
    assert(process_wait_child(&processes[0], -3, 3U, 0U, true,
        &complete) == 0 && complete);
    assert(syscall_process_signal(&processes[0], -3, 15) == 0);
    assert(processes[1].exiting && processes[2].exiting);
    processes[1].active = false;
    processes[1].zombie = true;
    processes[2].active = false;
    processes[2].zombie = true;
    assert(process_wait_child(&processes[0], -3, 3U, 0U, false,
        &complete) == 3 && complete);
    assert(process_wait_child(&processes[0], -3, 3U, 0U, false,
        &complete) == 4 && complete);
    assert(process_wait_child(&processes[0], -3, 3U, 0U, true,
        &complete) == -OPENRFS_ECHILD && complete);
    zero_bytes(&processes[3], sizeof(processes[3]));

    processes[1].generation = 3U;
    processes[1].parent_generation = 1U;
    processes[1].zombie = true;
    processes[2].generation = 4U;
    processes[2].parent_generation = 1U;
    processes[2].active = true;
    terminate_process(&processes[0], 0);
    assert(processes[1].generation == 0U);
    assert(processes[2].parent_generation == 0U);
    assert(processes[0].exiting);
    puts("Native process IDs, wait state, reaping and orphan lifetime: PASS");
    return 0;
}
