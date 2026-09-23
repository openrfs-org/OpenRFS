/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Run a function on another stack.
 *
 * Compatibility layers whose drivers give devices the addresses of stack
 * variables (SeaBIOS's) call into them on a stack inside their DMA arena.
 * This is in assembly for the reason thread.S is: a C function may assume it
 * returns on the stack it was called on, and this one deliberately runs its
 * callee elsewhere.
 */

.section .text, "ax"
.code64

/*
 * void hwdrv_call_on_stack(void (*function)(void *), void *argument,
 *                          void *stack_top)
 *
 * The caller's stack pointer is kept in RBP, which is callee-saved, so the
 * callee preserves it and the return lands on the original stack whatever the
 * callee did with its own. The new stack is aligned to 16 bytes before the
 * call, as the System V AMD64 ABI requires at every call site. The caller is
 * responsible for the interrupt state: with interrupts enabled an interrupt
 * frame would be pushed onto the new stack, which is legal but is exactly
 * what a layer running with interrupts disabled avoids.
 */
.global hwdrv_call_on_stack
.type hwdrv_call_on_stack, @function
hwdrv_call_on_stack:
    pushq %rbp
    movq %rsp, %rbp
    andq $-16, %rdx
    movq %rdx, %rsp
    movq %rdi, %rax
    movq %rsi, %rdi
    call *%rax
    movq %rbp, %rsp
    popq %rbp
    ret
.size hwdrv_call_on_stack, . - hwdrv_call_on_stack

.section .note.GNU-stack, "", @progbits
