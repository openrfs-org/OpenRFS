/* SPDX-License-Identifier: GPL-3.0-only */
.section .text, "ax", @progbits
.macro RSD_STUB name, count
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
RSD_STUB rsd_syscall0, 0
RSD_STUB rsd_syscall1, 1
RSD_STUB rsd_syscall2, 2
RSD_STUB rsd_syscall3, 3
RSD_STUB rsd_syscall4, 4
RSD_STUB rsd_syscall5, 5
RSD_STUB rsd_syscall6, 6
.section .note.GNU-stack, "", @progbits
