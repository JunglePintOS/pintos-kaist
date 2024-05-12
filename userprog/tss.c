#include "userprog/tss.h"
#include <debug.h>
#include <stddef.h>
#include "userprog/gdt.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "intrinsic.h"

/* 작업 상태 세그먼트 (TSS).
 *
 *  TSS의 인스턴스는 x86-64 특정 구조로, "작업"을 정의하는 데 사용됩니다.
 *  이는 프로세서에 내장된 다중 작업 지원 형태입니다. 그러나 이식성, 속도 및 유연성을 포함한
 *  여러 이유로 대부분의 x86-64 운영 체제는 TSS를 거의 완전히 무시합니다.
 *  우리도 그 예외가 아닙니다.
 *
 *  불행히도 TSS를 사용하여 수행할 수 있는 한 가지 작업이 있습니다:
 *  사용자 모드에서 발생한 인터럽트에 대한 스택 전환입니다.
 *  사용자 모드(ring 3)에서 인터럽트가 발생하면 프로세서는 현재 TSS의 rsp0 멤버를
 *  검사하여 인터럽트를 처리하는 데 사용할 스택을 결정합니다. 따라서 우리는
 *  TSS를 생성하고 적어도 이러한 필드를 초기화해야 하며, 이것이 정확히
 *  이 파일이 하는 일입니다.
 *
 *  인터럽트가 인터럽트 또는 트랩 게이트에 의해 처리될 때
 *  (우리가 다루는 모든 인터럽트에 해당),
 *  x86-64 프로세서는 다음과 같이 작동합니다:
 *
 *    - 인터럽트에 의해 중단된 코드가 인터럽트 핸들러와 같은 ring에 있는 경우,
 *      스택 전환이 발생하지 않습니다. 이는 우리가 커널에서 실행되는 동안 발생하는
 *      인터럽트에 해당합니다. TSS의 내용은 이 경우에는 무시됩니다.
 *
 *    - 인터럽트된 코드가 핸들러와 다른 ring에 있는 경우,
 *      프로세서는 새 ring을 위한 TSS에서 지정된 스택으로 전환합니다.
 *      이는 우리가 사용자 공간에 있는 동안 발생하는 인터럽트에 해당합니다.
 *      이미 사용 중인 스택을 사용하지 않도록 주의해야 합니다.
 *      현재 프로세스의 커널 스택이 사용되지 않음을 알고 있으므로 항상 해당 스택을 사용할 수 있습니다.
 *      따라서 스케줄러가 스레드를 전환할 때 TSS의 스택 포인터도 새 스레드의 커널 스택을 가리키도록 변경됩니다.
 *      (호출은 thread.c의 schedule에 있음) */
/* The Task-State Segment (TSS).
 *
 *  Instances of the TSS, an x86-64 specific structure, are used to
 *  define "tasks", a form of support for multitasking built right
 *  into the processor.  However, for various reasons including
 *  portability, speed, and flexibility, most x86-64 OSes almost
 *  completely ignore the TSS.  We are no exception.
 *
 *  Unfortunately, there is one thing that can only be done using
 *  a TSS: stack switching for interrupts that occur in user mode.
 *  When an interrupt occurs in user mode (ring 3), the processor
 *  consults the rsp0 members of the current TSS to determine the
 *  stack to use for handling the interrupt.  Thus, we must create
 *  a TSS and initialize at least these fields, and this is
 *  precisely what this file does.
 *
 *  When an interrupt is handled by an interrupt or trap gate
 *  (which applies to all interrupts we handle), an x86-64 processor
 *  works like this:
 *
 *    - If the code interrupted by the interrupt is in the same
 *      ring as the interrupt handler, then no stack switch takes
 *      place.  This is the case for interrupts that happen when
 *      we're running in the kernel.  The contents of the TSS are
 *      irrelevant for this case.
 *
 *    - If the interrupted code is in a different ring from the
 *      handler, then the processor switches to the stack
 *      specified in the TSS for the new ring.  This is the case
 *      for interrupts that happen when we're in user space.  It's
 *      important that we switch to a stack that's not already in
 *      use, to avoid corruption.  Because we're running in user
 *      space, we know that the current process's kernel stack is
 *      not in use, so we can always use that.  Thus, when the
 *      scheduler switches threads, it also changes the TSS's
 *      stack pointer to point to the new thread's kernel stack.
 *      (The call is in schedule in thread.c.) */

/* 커널 TSS. */
/* Kernel TSS. */
struct task_state *tss;

/* 커널 TSS를 초기화합니다. */
/* Initializes the kernel TSS. */
void
tss_init (void) {
	/* 우리의 TSS는 호출 게이트 또는 태스크 게이트에서 사용되지 않으므로
	 * 참조되는 것은 몇 가지 필드 뿐이며, 그 필드만 초기화합니다. */
	/* Our TSS is never used in a call gate or task gate, so only a
	 * few fields of it are ever referenced, and those are the only
	 * ones we initialize. */
	tss = palloc_get_page (PAL_ASSERT | PAL_ZERO);
	tss_update (thread_current ());
}

/* 커널 TSS를 반환합니다. */
/* Returns the kernel TSS. */
struct task_state *
tss_get (void) {
	ASSERT (tss != NULL);
	return tss;
}

/* TSS의 ring 0 스택 포인터를 스레드 스택의 끝을 가리키도록 설정합니다. */
/* Sets the ring 0 stack pointer in the TSS to point to the end
 * of the thread stack. */
void
tss_update (struct thread *next) {
	ASSERT (tss != NULL);
	tss->rsp0 = (uint64_t) next + PGSIZE;
}
