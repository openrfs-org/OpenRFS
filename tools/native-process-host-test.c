/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>

#include "../src/kernel/native_process.c"

int main(void)
{
    bool complete;
    struct native_pipe *pipe;
    uint64_t pipe_id;

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
    processes[1].generation = 2U;
    processes[1].parent_generation = 1U;
    processes[1].active = true;
    assert(process_wait_child(&processes[0], 2, 0U, true,
        &complete) == 0 && complete);
    assert(process_wait_child(&processes[0], 3, 0U, false,
        &complete) == -OPENRFS_ECHILD && complete);
    assert(process_wait_child(&processes[0], 2, 0U, false,
        &complete) == 0 && !complete);
    processes[1].active = false;
    processes[1].zombie = true;
    processes[1].exit_status = 23;
    assert(process_wait_child(&processes[0], -1, 0U, false,
        &complete) == 2 && complete);
    assert(processes[1].generation == 0U);
    assert(process_wait_child(&processes[0], -1, 0U, false,
        &complete) == -OPENRFS_ECHILD && complete);

    processes[1].generation = 3U;
    processes[1].parent_generation = 1U;
    processes[1].active = true;
    processes[2].generation = 4U;
    processes[2].parent_generation = 9U;
    processes[2].active = true;
    assert(syscall_process_signal(&processes[0], 4, 0) == -OPENRFS_EPERM);
    assert(syscall_process_signal(&processes[0], 5, 0) == -OPENRFS_ESRCH);
    assert(syscall_process_signal(&processes[0], 0, 15) == -OPENRFS_ENOSYS);
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
