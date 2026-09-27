/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>

#include "../src/kernel/native_process.c"

int main(void)
{
    bool complete;

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
