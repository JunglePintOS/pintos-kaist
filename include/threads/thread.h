#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif

/* 스레드의 생명 주기에 있는 상태들입니다. */
/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. *//* 실행 중인 스레드. */
	THREAD_READY,       /* Not running but ready to run. *//* 실행은 안 되지만 실행 준비는 된 상태. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. *//* 이벤트를 기다리는 상태. */
	THREAD_DYING        /* About to be destroyed. *//* 소멸될 예정인 상태. */
};

/* 스레드 식별자 타입. 원하는 타입으로 재정의 가능합니다. */
/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)           /* tid_t의 에러 값을 정의. *//* Error value for tid_t. */

/* 스레드 우선순위. */
/* Thread priorities. */
#define PRI_MIN 0                       /* 최소 우선순위. *//* Lowest priority. */
#define PRI_DEFAULT 31                  /* 기본 우선순위. *//* Default priority. */
#define PRI_MAX 63                       /* 최대 우선순위. *//* Highest priority. */


/* 커널 스레드 또는 유저 프로세스의 구조체입니다.
 *
 * 각 스레드 구조체는 자신의 4 kB 페이지에 저장됩니다. 스레드 구조체 자체는
 * 페이지의 맨 아래(오프셋 0)에 위치합니다. 페이지의 나머지 부분은
 * 스레드의 커널 스택을 위해 예약되어 있으며, 이는 페이지의 상단(오프셋 4 kB)에서
 * 아래 방향으로 성장합니다. 다음은 이를 도식화한 것입니다:
 *
 *      4 kB +---------------------------------+
 *           |          커널 스택               |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         아래 방향으로 성장       |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               이름              |
 *           |              상태               |
 *      0 kB +---------------------------------+
 *
 * 이 구조는 두 가지 중요한 점을 시사합니다:
 *
 *    1. `struct thread`는 너무 커져서는 안 됩니다. 크기가 커지면
 *       커널 스택에 충분한 공간이 없게 됩니다. 기본 `struct thread`는
 *       몇 바이트 크기에 불과하며, 1 kB 미만으로 유지되어야 합니다.
 *
 *    2. 커널 스택은 너무 커져서도 안 됩니다. 스택 오버플로우가 발생하면
 *       스레드 상태가 손상될 수 있습니다. 따라서, 커널 함수는 큰 구조체나
 *       배열을 비정적 로컬 변수로 할당해서는 안 됩니다. 대신 malloc()이나
 *       palloc_get_page()를 사용하여 동적 할당을 수행하세요.
 *
 * 이러한 문제들의 첫 번째 증상은 아마도 thread_current()에서의
 * 단언 실패일 것입니다. 이 함수는 실행 중인 스레드의 `struct thread`의
 * `magic` 멤버가 THREAD_MAGIC으로 설정되어 있는지를 확인합니다.
 * 스택 오버플로우는 보통 이 값을 변경하여 단언을 유발합니다. */

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */

/* 이 두 가지 사용 방법이 가능한 이유는, 각 상태가 서로 배타적이기 때문입니다.
	 준비 상태(ready state)에 있는 스레드만 실행 대기열에 있을 수 있고, 
	 차단된 상태(blocked state)에 있는 스레드만 세마포어 대기 목록에 있을 수 있습니다. 
	 이를 통해 elem 멤버는 효율적으로 두 가지 목적으로 사용될 수 있으며, 
	 각 상황에 따라 적절하게 스레드를 관리하는 데 필요한 구조적 지원을 제공합니다.*/
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
	/* thread.c에 의해 소유됨. *//* Owned by thread.c. */
	tid_t tid;                          /* 스레드 식별자. *//* Thread identifier. */
	enum thread_status status;          /* 스레드 상태. *//* Thread state. */
	char name[16];                      /* 디버깅 목적의 이름. *//* Name (for debugging purposes). */
	int priority;                       /* 우선순위. *//* Priority. */

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* 리스트 요소. *//* List element. */

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* 페이지 맵 레벨 4 *//* Page map level 4 */
#endif
#ifdef VM
	/* 스레드가 소유한 전체 가상 메모리에 대한 테이블. */
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* 문맥 교환을 위한 정보 *//* Information for switching */
	unsigned magic;                     /* 스택 오버플로우 감지. *//* Detects stack overflow. */
};

/* false(기본값)인 경우, 라운드-로빈 스케줄러를 사용합니다.
   true인 경우, 다중 수준 피드백 큐 스케줄러를 사용합니다.
   커널 명령 줄 옵션 "-o mlfqs"에 의해 제어됩니다. */
/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

#endif /* threads/thread.h */
