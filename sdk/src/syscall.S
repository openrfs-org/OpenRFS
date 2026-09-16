/* SPDX-License-Identifier: GPL-3.0-only */
.section .text, "ax", @progbits
.macro OPENGAT_STUB name, count
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
OPENGAT_STUB opengat_syscall0, 0
OPENGAT_STUB opengat_syscall1, 1
OPENGAT_STUB opengat_syscall2, 2
OPENGAT_STUB opengat_syscall3, 3
OPENGAT_STUB opengat_syscall4, 4
OPENGAT_STUB opengat_syscall5, 5
OPENGAT_STUB opengat_syscall6, 6
.section .note.GNU-stack, "", @progbits
