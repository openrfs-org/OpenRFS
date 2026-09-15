/* SPDX-License-Identifier: GPL-3.0-only */
.section .text, "ax", @progbits
.macro TRAIT_STUB name, count
.global \name
.type \name, @function
\name:
    movq %rdi, %rax
.if \count > 0
    movq %rsi, %rdi
.endif
.if \count > 1
    movq %rdx, %rsi
.endif
.if \count > 2
    movq %rcx, %rdx
.endif
.if \count > 3
    movq %r8, %r10
.endif
.if \count > 4
    movq %r9, %r8
.endif
.if \count > 5
    movq 8(%rsp), %r9
.endif
    syscall
    ret
.size \name, . - \name
.endm
TRAIT_STUB trait_syscall0, 0
TRAIT_STUB trait_syscall1, 1
TRAIT_STUB trait_syscall2, 2
TRAIT_STUB trait_syscall3, 3
TRAIT_STUB trait_syscall4, 4
TRAIT_STUB trait_syscall5, 5
TRAIT_STUB trait_syscall6, 6
.section .note.GNU-stack, "", @progbits
