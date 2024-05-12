#include "threads/loader.h"

.text
.globl syscall_entry
.type syscall_entry, @function
syscall_entry:
	movq %rbx, temp1(%rip)
	movq %r12, temp2(%rip)     /* callee saved registers */
	movq %rsp, %rbx            /* 사용자 모드의 rsp를 저장합니다 *//* Store userland rsp    */
	movabs $tss, %r12
	movq (%r12), %r12
	movq 4(%r12), %rsp         /* tss에서 ring0 rsp를 읽어옵니다 *//* Read ring0 rsp from the tss */
	/* Now we are in the kernel stack */
	push $(SEL_UDSEG)      /* if->ss */
	push %rbx              /* if->rsp */
	push %r11              /* if->eflags */
	push $(SEL_UCSEG)      /* if->cs */
	push %rcx              /* if->rip */
	subq $16, %rsp         /* error_code, vec_no를 건너뜁니다 *//* skip error_code, vec_no */
	push $(SEL_UDSEG)      /* if->ds */
	push $(SEL_UDSEG)      /* if->es */
	push %rax
	movq temp1(%rip), %rbx
	push %rbx
	pushq $0
	push %rdx
	push %rbp
	push %rdi
	push %rsi
	push %r8
	push %r9
	push %r10
	pushq $0 /* r11을 건너뜁니다 *//* skip r11 */
	movq temp2(%rip), %r12
	push %r12
	push %r13
	push %r14
	push %r15
	movq %rsp, %rdi

check_intr:
	btsq $9, %r11          /* 인터럽트가 복원되었는지 확인합니다 *//* Check whether we recover the interrupt */
	jnb no_sti
	sti                    /* 인터럽트를 복원합니다 *//* restore interrupt */
no_sti:
	movabs $syscall_handler, %r12
	call *%r12
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rsi
	popq %rdi
	popq %rbp
	popq %rdx
	popq %rcx
	popq %rbx
	popq %rax
	addq $32, %rsp
	popq %rcx              /* if->rip */
	addq $8, %rsp
	popq %r11              /* if->eflags */
	popq %rsp              /* if->rsp */
	sysretq

.section .data
.globl temp1
temp1:
.quad	0
.globl temp2
temp2:
.quad	0

/* syscall_entry 함수의 설명:
 * - syscall_entry는 유저 모드에서의 시스템 호출을 처리하는 어셈블리 함수입니다.
 * - 이 함수는 유저 모드에서의 시스템 호출을 받아들이고, 커널 모드로 전환하여 해당 시스템 호출을 처리합니다.
 * - syscall_entry는 운영체제의 특정한 인터럽트 벡터에서 호출되어야 합니다.
 * - 시스템 호출을 처리하기 위해 호출되는 syscall_handler 함수는 아직 구현되지 않았습니다.
 * - 이 함수는 인자를 저장하고 커널 스택으로 전환한 후, syscall_handler를 호출합니다.
 */