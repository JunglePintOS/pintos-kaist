#include "threads/thread.h"

#include <debug.h>
#include <random.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "intrinsic.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* struct thread의 `magic` 멤버를 위한 랜덤 값.
   스택 오버플로우를 감지하는데 사용됩니다. 자세한 내용은 thread.h의 상단 주석을 참조하세요. */
/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* 기본 스레드를 위한 랜덤 값입니다.
   이 값을 수정하지 마세요. */
/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* MEMBER */
#define PRIORITY "priority"

/* THREAD_READY 상태인 프로세스 목록, 즉 실행할 준비는 되었지만 실제로 실행 중이지 않은 프로세스입니다. */
/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

static struct list sleep_list;
static int64_t next_tick_to_awake = NULL;

/* 유휴 스레드. */
/* Idle thread. */
static struct thread *idle_thread;

/* 초기 스레드, init.c의 main()을 실행하는 스레드. */
/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* tid를 할당할 때 사용되는 락. */
/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* 스레드 파괴 요청 목록 */
/* Thread destruction requests */
static struct list destruction_req;

/* 통계. */
/* Statistics. */
static long long idle_ticks; /* 유휴 상태에서 보낸 타이머 틱 수. */       /* # of timer ticks spent idle. */
static long long kernel_ticks; /* 커널 스레드에서 보낸 타이머 틱 수. */   /* # of timer ticks in kernel threads. */
static long long user_ticks; /* 사용자 프로그램에서 보낸 타이머 틱 수. */ /* # of timer ticks in user programs. */

/* 스케줄링. */
/* Scheduling. */
#define TIME_SLICE 4 /* 각 스레드에게 주어지는 타이머 틱 수. */     /* # of timer ticks to give each thread. */
static unsigned thread_ticks; /* 마지막 yield 이후 타이머 틱 수. */ /* # of timer ticks since last yield. */

/* false(기본값)면 라운드-로빈 스케줄러를 사용합니다.
   true면 다중 레벨 피드백 큐 스케줄러를 사용합니다.
   커널 명령줄 옵션 "-o mlfqs"에 의해 제어됩니다. */
/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread(thread_func *, void *aux);

static void idle(void *aux UNUSED);
static struct thread *next_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule(void);
static tid_t allocate_tid(void);
void thread_sleep(int64_t ticks);
void thread_wakeup(int64_t ticks);

/* T가 유효한 스레드를 가리키는지 확인합니다. */
/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* 실행 중인 스레드를 반환합니다.
 * CPU의 스택 포인터 `rsp`를 읽고, 페이지의 시작으로 내림합니다.
 * `struct thread`는 항상 페이지의 시작에 있고 스택 포인터는 중간 어딘가에 있으므로,
 * 이를 통해 현재 스레드를 찾을 수 있습니다. */
/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *)(pg_round_down(rrsp())))

// 스레드 시작을 위한 전역 설명자 테이블.
// 스레드 초기화 후 gdt가 설정되기 때문에, 임시 gdt를 먼저 설정해야 합니다.
// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = {0, 0x00af9a000000ffff, 0x00cf92000000ffff};

/* 스레딩 시스템을 초기화하여 현재 실행 중인 코드를 스레드로 변환합니다.
   이는 일반적으로 작동하지 않지만, loader.S가 스택의 하단을 페이지 경계에
   맞추어 놓았기 때문에 이 경우에만 가능합니다.

   또한 실행 큐와 tid 락을 초기화합니다.

   이 함수를 호출한 후에는 페이지 할당자를 초기화하기 전에
   thread_create()로 스레드를 생성하려고 하지 마세요.

   이 함수가 완료될 때까지 thread_current()를 호출하는 것은 안전하지 않습니다. */
/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void thread_init(void) {
    ASSERT(intr_get_level() == INTR_OFF);

    /* 커널을 위한 임시 gdt를 다시 로드합니다.
     * 이 gdt는 사용자 컨텍스트를 포함하지 않습니다.
     * 커널은 gdt_init()에서 사용자 컨텍스트와 함께 gdt를 재구성합니다. */
    /* Reload the temporal gdt for the kernel
     * This gdt does not include the user context.
     * The kernel will rebuild the gdt with user context, in gdt_init (). */
    struct desc_ptr gdt_ds = {.size = sizeof(gdt) - 1, .address = (uint64_t)gdt};
    lgdt(&gdt_ds);

    /* 전역 스레드 컨텍스트 초기화 */
    /* Init the globla thread context */
    lock_init(&tid_lock);
    list_init(&ready_list);
    list_init(&sleep_list);
    list_init(&destruction_req);

    /* 실행 중인 스레드를 위한 스레드 구조체 설정 */
    /* Set up a thread structure for the running thread. */
    initial_thread = running_thread();
    init_thread(initial_thread, "main", PRI_DEFAULT);
    initial_thread->status = THREAD_RUNNING;
    initial_thread->tid = allocate_tid();
}

// 스레드를 sleep 시켜서 sleep_list에 넣습니다.
void thread_sleep(int64_t ticks) {
    struct thread *curr = thread_current();
    enum intr_level old_level;

    old_level = intr_disable();

    if (curr != idle_thread) {
        curr->wakeup_ticks = ticks;
        list_push_front(&sleep_list, &curr->elem);
        next_awake_ticks(curr->wakeup_ticks);
        thread_block();
    }

    intr_set_level(old_level);
}

// 깨어날 thread wakeup_ticks 중에서 작거나 같은 것을 wakeup_ticks를 업데이트
void next_awake_ticks(int64_t ticks) {
    if (next_tick_to_awake == NULL)
        next_tick_to_awake = ticks;
    if (ticks < next_tick_to_awake)
        next_tick_to_awake = ticks;
}

// 일어날 시간이 되면 대기 큐에 업데이트 후 sleep_list에서 remove
void thread_wakeup(int64_t ticks) {
    struct list_elem *e = list_begin(&sleep_list);  // sleep_list 시작부터 순회
    
    if (ticks < next_tick_to_awake)
        return;

    while (e != list_end(&sleep_list))  // sleep_list에 마지막까지 봄
    {
        struct thread *t = list_entry(e, struct thread, elem);

        if (t->wakeup_ticks <= ticks) {
            e = list_remove(e);
            thread_unblock(t);
        }
        else {
            e = list_next(e);
        }
    }
}

/* 선점형 스레드 스케줄링을 시작하기 위해 인터럽트를 활성화합니다.
   또한 유휴 스레드를 생성합니다. */
/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void thread_start(void) {
    /* 유휴 스레드 생성 */
    /* Create the idle thread. */
    struct semaphore idle_started;
    sema_init(&idle_started, 0);
    thread_create("idle", PRI_MIN, idle, &idle_started);

    /* 선점형 스레드 스케줄링 시작 */
    /* Start preemptive thread scheduling. */
    intr_enable();

    /* 유휴 스레드가 idle_thread를 초기화할 때까지 기다립니다. */
    /* Wait for the idle thread to initialize idle_thread. */
    sema_down(&idle_started);
}

/* 타이머 인터럽트 핸들러가 각 타이머 틱마다 호출합니다.
   따라서, 이 함수는 외부 인터럽트 컨텍스트에서 실행됩니다. */
/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void thread_tick(void) {
    struct thread *t = thread_current();

    /* 통계 업데이트 */
    /* Update statistics. */
    if (t == idle_thread)
        idle_ticks++;
#ifdef USERPROG
    else if (t->pml4 != NULL)
        user_ticks++;
#endif
    else
        kernel_ticks++;

    /* 선점 강제 실행 */
    /* Enforce preemption. */
    if (++thread_ticks >= TIME_SLICE)
        intr_yield_on_return();
}

/* 스레드 통계를 출력합니다. */
/* Prints thread statistics. */
void thread_print_stats(void) {
    printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n", idle_ticks, kernel_ticks, user_ticks);
}

/* NAME이라는 이름의 새 커널 스레드를 생성하고 주어진 초기
   PRIORITY로 FUNCTION을 실행하며, AUX를 인자로 전달하고,
   준비 큐에 추가합니다. 새 스레드의 스레드 식별자를 반환하거나,
   생성에 실패하면 TID_ERROR를 반환합니다.

   thread_start()가 호출된 경우, 새 스레드가 thread_create()가
   반환되기 전에 스케줄될 수 있습니다. 심지어 thread_create()가
   반환되기 전에 종료될 수도 있습니다. 반대로, 원래 스레드는
   새 스레드가 스케줄되기 전에 어느 정도 시간 동안 실행될 수 있습니다.
   순서를 보장해야 한다면 세마포어 또는 동기화의 다른 형태를 사용하세요.

   제공된 코드는 새 스레드의 `priority` 멤버를 PRIORITY로 설정하지만,
   실제 우선순위 스케줄링은 구현되지 않았습니다.
   우선순위 스케줄링은 문제 1-3의 목표입니다. */
/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t thread_create(const char *name, int priority, thread_func *function, void *aux) {
    struct thread *t;
    tid_t tid;

    ASSERT(function != NULL);

    /* 스레드 할당. */
    /* Allocate thread. */
    t = palloc_get_page(PAL_ZERO);
    if (t == NULL)
        return TID_ERROR;

    /* 스레드 초기화. */
    /* Initialize thread. */
    init_thread(t, name, priority);
    tid = t->tid = allocate_tid();

    /* kernel_thread 호출 시 스케줄링됩니다.
     * 주의) rdi는 첫 번째 인자이며, rsi는 두 번째 인자입니다. */
    /* Call the kernel_thread if it scheduled.
     * Note) rdi is 1st argument, and rsi is 2nd argument. */
    t->tf.rip = (uintptr_t)kernel_thread;
    t->tf.R.rdi = (uint64_t)function;
    t->tf.R.rsi = (uint64_t)aux;
    t->tf.ds = SEL_KDSEG;
    t->tf.es = SEL_KDSEG;
    t->tf.ss = SEL_KDSEG;
    t->tf.cs = SEL_KCSEG;
    t->tf.eflags = FLAG_IF;

    // for project 2 sys call
    t->fdt = palloc_get_multiple(PAL_ZERO, FDT_PAGES);
    if (t->fdt == NULL) {
        return TID_ERROR;
    }
    t->fd_idx = 2;
    t->fdt[0] = 1;  //stdin
    t->fdt[1] = 2;  //stdout

    /* 준비 큐에 추가. */
    /* Add to run queue. */
    thread_unblock(t);

    // 우선순위 스케줄링
    test_max_priority();

    return tid;
}

/* 현재 스레드를 슬립 상태로 전환합니다. thread_unblock()에 의해 다시 스케줄되기 전까지
   스케줄되지 않습니다.

   이 함수는 인터럽트가 꺼진 상태에서 호출되어야 합니다. 일반적으로 synch.h의 동기화
   기본이 더 좋은 생각입니다. */
/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void thread_block(void) {
    ASSERT(!intr_context());
    ASSERT(intr_get_level() == INTR_OFF);
    thread_current()->status = THREAD_BLOCKED;
    schedule();
}

/* 차단된 스레드 T를 실행 준비가 된 상태로 전환합니다.
   이는 T가 차단되지 않은 경우에는 오류입니다. (실행 중인 스레드를 준비 상태로 만들려면
   thread_yield()를 사용하세요.)

   이 함수는 실행 중인 스레드를 선점하지 않습니다. 이는 중요할 수 있습니다. 호출자가
   스스로 인터럽트를 비활성화한 경우, 스레드를 차단 해제하고 다른 데이터를 원자적으로
   업데이트할 수 있기를 기대할 수 있습니다. */
/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void thread_unblock(struct thread *t) {
    enum intr_level old_level;

    ASSERT(is_thread(t));

    old_level = intr_disable();
    ASSERT(t->status == THREAD_BLOCKED);
    // list_push_back(&ready_list, &t->elem);
    list_insert_desc_ordered(&ready_list, &t->elem, less_priority, PRIORITY);
    t->status = THREAD_READY;
    intr_set_level(old_level);
}

/* 값 비교. t1의 우선순위가 낮은 경우 true*/
bool less_priority(struct list_elem *e1, struct list_elem *e2, void *aux) {
    struct thread *t1 = list_entry(e1, struct thread, elem);
    struct thread *t2 = list_entry(e2, struct thread, elem);
    int t1_priority = t1->priority;
    int t2_priority = t2->priority;

    if (t1_priority < t2_priority) {
        return true;
    }
    return false;
}
/* 값 비교. t1의 우선순위가 낮은 경우 false*/
bool more_priority(struct list_elem *e1, struct list_elem *e2, void *aux) {
    struct thread *t1 = list_entry(e1, struct thread, elem);
    struct thread *t2 = list_entry(e2, struct thread, elem);
    int t1_priority = t1->priority;
    int t2_priority = t2->priority;

    if (t1_priority > t2_priority) {
        return true;
    }
    return false;
}

// 우선순위 스케줄링 하는 함수
void test_max_priority(void) {
    struct thread *curr = thread_current();
    struct list_elem *highest_elem = list_begin(&ready_list);
    if (list_end(&ready_list) == (highest_elem)) {
        return;
    }
    // 인터럽트 컨텍스트가 아니고, 현재 스레드의 우선순위가 준비 리스트의 최고 우선순위 스레드보다 낮다면
    if (!intr_context() && less_priority(&curr->elem, highest_elem, NULL)) {
        thread_yield();
    }
}

/* 실행 중인 스레드의 이름을 반환합니다. */
/* Returns the name of the running thread. */
const char *thread_name(void) {
    return thread_current()->name;
}

/* 실행 중인 스레드를 반환합니다.
   이는 running_thread()에 몇 가지 적절성 검사를 추가한 것입니다.
   자세한 내용은 thread.h 상단의 주석을 참조하세요. */
/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *thread_current(void) {
    struct thread *t = running_thread();

    /* T가 실제로 스레드인지 확인하세요.
      이러한 주장 중 하나라도 발생하면 스레드가 스택을 오버플로할 수 있습니다.
      각 스레드는 4kB 미만의 스택을 가지므로 몇 개의 큰 자동 배열이나
      적당한 재귀는 스택 오버플로를 일으킬 수 있습니다. */
    /* Make sure T is really a thread.
       If either of these assertions fire, then your thread may
       have overflowed its stack.  Each thread has less than 4 kB
       of stack, so a few big automatic arrays or moderate
       recursion can cause stack overflow. */
    ASSERT(is_thread(t));
    ASSERT(t->status == THREAD_RUNNING);

    return t;
}

/* 실행 중인 스레드의 tid를 반환합니다. */
/* Returns the running thread's tid. */
tid_t thread_tid(void) {
    return thread_current()->tid;
}

/* 현재 스레드의 스케줄을 취소하고 파괴합니다. 호출자에게 다시 반환되지 않습니다. */
/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void thread_exit(void) {
    ASSERT(!intr_context());

#ifdef USERPROG
    process_exit();
#endif

    /* 상태를 dying으로 설정하고 다른 프로세스를 스케줄합니다.
       schedule_tail() 호출 중에 파괴됩니다. */
    /* Just set our status to dying and schedule another process.
       We will be destroyed during the call to schedule_tail(). */
    intr_disable();
    do_schedule(THREAD_DYING);
    NOT_REACHED();
}

/* CPU를 양보합니다. 현재 스레드는 슬립 상태로 전환되지 않으며,
   스케줄러의 변덕에 따라 즉시 다시 스케줄될 수 있습니다. */
/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void thread_yield(void) {
    struct thread *curr = thread_current();
    enum intr_level old_level;

    ASSERT(!intr_context());
    
    old_level = intr_disable();
    if (curr != idle_thread) {
        // list_push_back(&ready_list, &curr->elem);
        list_insert_desc_ordered(&ready_list, &curr->elem, less_priority, PRIORITY);
    }
    do_schedule(THREAD_READY);
    intr_set_level(old_level);
}

/* 현재 스레드의 우선순위를 NEW_PRIORITY로 설정합니다. */
/* Sets the current thread's priority to NEW_PRIORITY. */
void thread_set_priority(int new_priority) {
    thread_current()->init_priority = new_priority;

    refresh_priority();
    test_max_priority();
}

/* 현재 스레드의 우선순위를 반환합니다. */
/* Returns the current thread's priority. */
int thread_get_priority(void) {
    return thread_current()->priority;
}

/* 현재 스레드의 nice 값을 NICE로 설정합니다. */
/* Sets the current thread's nice value to NICE. */
void thread_set_nice(int nice UNUSED) {
    /* TODO: Your implementation goes here */
}

/* 현재 스레드의 nice 값을 반환합니다. */
/* Returns the current thread's nice value. */
int thread_get_nice(void) {
    /* TODO: Your implementation goes here */
    return 0;
}

/* 시스템 로드 평균의 100배를 반환합니다. */
/* Returns 100 times the system load average. */
int thread_get_load_avg(void) {
    /* TODO: Your implementation goes here */
    return 0;
}

/* 현재 스레드의 recent_cpu 값의 100배를 반환합니다. */
/* Returns 100 times the current thread's recent_cpu value. */
int thread_get_recent_cpu(void) {
    /* TODO: Your implementation goes here */
    return 0;
}

/* 유휴 스레드입니다. 다른 스레드가 실행 준비가 되어 있지 않을 때 실행됩니다.

   유휴 스레드는 thread_start()에 의해 처음에 준비 목록에 추가됩니다.
   처음에 한 번 스케줄되면 idle_thread를 초기화하고, thread_start()가
   계속될 수 있도록 전달된 세마포어를 "up"하고 즉시 차단됩니다.
   그 후, 유휴 스레드는 준비 목록에 다시 나타나지 않습니다.
   준비 목록이 비어 있을 때 next_thread_to_run()에 의해
   특별한 경우로 반환됩니다. */
/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void idle(void *idle_started_ UNUSED) {
    struct semaphore *idle_started = idle_started_;

    idle_thread = thread_current();
    sema_up(idle_started);

    for (;;) {
        /* 다른 사람에게 실행을 양보합니다. */
        /* Let someone else run. */
        intr_disable();
        thread_block();

        /* 인터럽트를 다시 활성화하고 다음 인터럽트를 기다립니다.

            'sti' 명령은 다음 명령의 완료까지 인터럽트를 비활성화하므로,
            이 두 명령은 원자적으로 실행됩니다. 이 원자성은 중요합니다.
            그렇지 않으면, 인터럽트가 다시 활성화되고 다음 인터럽트가 발생하기를 기다리는 사이에
            인터럽트가 처리될 수 있으며, 최대 한 클록 틱의 시간을 낭비할 수 있습니다.

            [IA32-v2a] "HLT", [IA32-v2b] "STI", [IA32-v3a] 7.11.1 "HLT 명령"을 참조하세요. */
        /* Re-enable interrupts and wait for the next one.

           The `sti' instruction disables interrupts until the
           completion of the next instruction, so these two
           instructions are executed atomically.  This atomicity is
           important; otherwise, an interrupt could be handled
           between re-enabling interrupts and waiting for the next
           one to occur, wasting as much as one clock tick worth of
           time.

           See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
           7.11.1 "HLT Instruction". */
        asm volatile("sti; hlt" : : : "memory");
    }
}

/* 커널 스레드의 기초가 되는 함수입니다. */
/* Function used as the basis for a kernel thread. */
static void kernel_thread(thread_func *function, void *aux) {
    ASSERT(function != NULL);

    intr_enable(); /* 스케줄러는 인터럽트를 끈 상태에서 실행됩니다. */ /* The scheduler runs with interrupts off. */
    function(aux); /* 스레드 함수를 실행합니다. */                     /* Execute the thread function. */
    thread_exit(); /* function()이 반환하면 스레드를 종료합니다. */    /* If function() returns, kill the thread. */
}

/* T를 차단된 스레드로 초기화하고 NAME으로 이름을 지정합니다. */
/* Does basic initialization of T as a blocked thread named
   NAME. */
static void init_thread(struct thread *t, const char *name, int priority) {
    ASSERT(t != NULL);
    ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
    ASSERT(name != NULL);

    memset(t, 0, sizeof *t);
    t->status = THREAD_BLOCKED;
    strlcpy(t->name, name, sizeof t->name);
    t->tf.rsp = (uint64_t)t + PGSIZE - sizeof(void *);
    t->priority = priority;
    t->init_priority = priority;
    t->wait_on_lock = NULL;
    list_init (&t->donations);
    t->magic = THREAD_MAGIC;
}

/* 실행할 다음 스레드를 선택하고 반환합니다. 실행 큐에서 스레드를 반환해야 합니다.
   실행 큐가 비어 있지 않다면, 실행 중인 스레드가 계속 실행될 수 있습니다.
   실행 큐가 비어 있다면, idle_thread를 반환합니다. */
/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *next_thread_to_run(void) {
    if (list_empty(&ready_list))
        return idle_thread;
    else
        return list_entry(list_pop_front(&ready_list), struct thread, elem);
}

/* iretq를 사용하여 스레드를 시작합니다. */
/* Use iretq to launch the thread */
void do_iret(struct intr_frame *tf) {
    __asm __volatile(
        "movq %0, %%rsp\n"
        "movq 0(%%rsp),%%r15\n"
        "movq 8(%%rsp),%%r14\n"
        "movq 16(%%rsp),%%r13\n"
        "movq 24(%%rsp),%%r12\n"
        "movq 32(%%rsp),%%r11\n"
        "movq 40(%%rsp),%%r10\n"
        "movq 48(%%rsp),%%r9\n"
        "movq 56(%%rsp),%%r8\n"
        "movq 64(%%rsp),%%rsi\n"
        "movq 72(%%rsp),%%rdi\n"
        "movq 80(%%rsp),%%rbp\n"
        "movq 88(%%rsp),%%rdx\n"
        "movq 96(%%rsp),%%rcx\n"
        "movq 104(%%rsp),%%rbx\n"
        "movq 112(%%rsp),%%rax\n"
        "addq $120,%%rsp\n"
        "movw 8(%%rsp),%%ds\n"
        "movw (%%rsp),%%es\n"
        "addq $32, %%rsp\n"
        "iretq"
        :
        : "g"((uint64_t)tf)
        : "memory");
}

/* 새 스레드의 페이지 테이블을 활성화하고 이전 스레드를 종료 상태로 전환합니다.
   이 함수가 호출되었을 때, 이미 새 스레드는 실행 중이며 인터럽트는 비활성화 상태입니다.
   스레드 전환이 완전히 끝날 때까지 printf() 호출은 안전하지 않습니다.
   이는 실제로 printf() 호출은 함수의 맨 끝에서만 수행되어야 함을 의미합니다. */
/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void thread_launch(struct thread *th) {
    uint64_t tf_cur = (uint64_t)&running_thread()->tf;  // 현재 실행 중인 스레드의 프레임 주소를 가져옵니다.
    uint64_t tf = (uint64_t)&th->tf;                    // 전환될 새 스레드의 프레임 주소를 가져옵니다.
    ASSERT(intr_get_level() == INTR_OFF);               // 인터럽트가 비활성화되었는지 확인합니다.

    /* 주 스위칭 로직입니다.
     * 먼저 전체 실행 컨텍스트를 intr_frame으로 복원하고 do_iret를 호출하여 다음 스레드로 전환합니다.
     * 여기서 스위칭이 완료될 때까지 스택을 사용해서는 안 됩니다. */
    /* The main switching logic.
     * We first restore the whole execution context into the intr_frame
     * and then switching to the next thread by calling do_iret.
     * Note that, we SHOULD NOT use any stack from here
     * until switching is done. */
    __asm __volatile(
        /* 사용될 레지스터를 저장합니다. */ 
        /* Store registers that will be used. */
        // 레지스터에 저장된 값을 스택에 밀어넣기
        "push %%rax\n"
        "push %%rbx\n"
        "push %%rcx\n"
        /* 입력을 한 번만 가져옵니다. */
        /* Fetch input once */
        "movq %0, %%rax\n"
        "movq %1, %%rcx\n"
        "movq %%r15, 0(%%rax)\n"
        "movq %%r14, 8(%%rax)\n"
        "movq %%r13, 16(%%rax)\n"
        "movq %%r12, 24(%%rax)\n"
        "movq %%r11, 32(%%rax)\n"
        "movq %%r10, 40(%%rax)\n"
        "movq %%r9, 48(%%rax)\n"
        "movq %%r8, 56(%%rax)\n"
        "movq %%rsi, 64(%%rax)\n"
        "movq %%rdi, 72(%%rax)\n"
        "movq %%rbp, 80(%%rax)\n"
        "movq %%rdx, 88(%%rax)\n"
        "pop %%rbx\n"  // Saved rcx
        "movq %%rbx, 96(%%rax)\n"
        "pop %%rbx\n"  // Saved rbx
        "movq %%rbx, 104(%%rax)\n"
        "pop %%rbx\n"  // Saved rax
        "movq %%rbx, 112(%%rax)\n"
        "addq $120, %%rax\n"
        "movw %%es, (%%rax)\n"
        "movw %%ds, 8(%%rax)\n"
        "addq $32, %%rax\n"
        "call __next\n"  // read the current rip.
        "__next:\n"
        "pop %%rbx\n"
        "addq $(out_iret -  __next), %%rbx\n"
        "movq %%rbx, 0(%%rax)\n"  // rip
        "movw %%cs, 8(%%rax)\n"   // cs
        "pushfq\n"
        "popq %%rbx\n"
        "mov %%rbx, 16(%%rax)\n"  // eflags
        "mov %%rsp, 24(%%rax)\n"  // rsp
        "movw %%ss, 32(%%rax)\n"
        "mov %%rcx, %%rdi\n"
        "call do_iret\n"
        "out_iret:\n"
        :
        : "g"(tf_cur), "g"(tf)
        : "memory");
}

/* 새 프로세스를 스케줄링합니다. 진입 시, 인터럽트는 꺼져 있어야 합니다.
 * 이 함수는 현재 스레드의 상태를 status로 변경한 다음,
 * 다른 스레드를 찾아 전환합니다.
 * schedule()에서 printf()를 호출하는 것은 안전하지 않습니다. */
/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void do_schedule(int status) {
    ASSERT(intr_get_level() == INTR_OFF);
    ASSERT(thread_current()->status == THREAD_RUNNING);
    while (!list_empty(&destruction_req)) {
        struct thread *victim = list_entry(list_pop_front(&destruction_req), struct thread, elem);
        palloc_free_page(victim);
    }
    thread_current()->status = status;
    schedule();
}

/* 스케줄러의 핵심 함수로, 현재 실행 중인 스레드에서 다음 실행할 스레드로 전환합니다.
   이 함수는 인터럽트가 비활성화된 상태에서 호출되어야 합니다.
   이 함수의 호출은 현재 실행 중인 스레드의 상태가 '실행 중'이 아니라고 가정합니다.
   또한, 다음에 실행될 스레드가 유효한 스레드 객체인지 확인합니다. */
static void schedule(void) {
    struct thread *curr = running_thread();      // 현재 실행 중인 스레드를 가져옵니다.
    struct thread *next = next_thread_to_run();  // 다음에 실행할 스레드를 결정합니다.

    ASSERT(intr_get_level() == INTR_OFF);    // 인터럽트가 비활성화되었는지 확인합니다.
    ASSERT(curr->status != THREAD_RUNNING);  // 현재 스레드의 상태가 실행 중이 아닌지 확인합니다.
    ASSERT(is_thread(next));                 // 다음 스레드가 유효한 스레드인지 확인합니다.

    /* 다음 스레드의 상태를 '실행 중'으로 설정합니다. */
    /* Mark us as running. */
    next->status = THREAD_RUNNING;

    /* 새로운 시간 할당량을 시작합니다. */
    /* Start new time slice. */
    thread_ticks = 0;

#ifdef USERPROG
    /* 새로운 주소 공간을 활성화합니다. */
    /* Activate the new address space. */
    process_activate(next);
#endif

    if (curr != next) {
        /* 현재 스레드가 종료 상태인 경우, 그 스레드를 파괴합니다.
           이 작업은 thread_exit() 함수가 자기 자신을 종료하지 않도록 늦게 수행됩니다.
           페이지 해제 요청은 여기서 큐에 저장되며, 실제 파괴 로직은 schedule()의 시작 부분에서 호출됩니다. */
        /* If the thread we switched from is dying, destroy its struct
           thread. This must happen late so that thread_exit() doesn't
           pull out the rug under itself.
           We just queuing the page free reqeust here because the page is
           currently used by the stack.
           The real destruction logic will be called at the beginning of the
           schedule(). */
        if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
            ASSERT(curr != next);
            list_push_back(&destruction_req, &curr->elem);
        }

        /* 전환할 스레드를 선택한 후, 우리는 먼저 현재 실행 중인 스레드의 정보를 저장합니다. */
        /* Before switching the thread, we first save the information
         * of current running. */
        thread_launch(next);
    }
}

/* 새 스레드를 위한 tid를 반환합니다. */
/* Returns a tid to use for a new thread. */
static tid_t allocate_tid(void) {
    static tid_t next_tid = 1;
    tid_t tid;

    lock_acquire(&tid_lock);
    tid = next_tid++;
    lock_release(&tid_lock);

    return tid;
}

bool thread_compare_donate_priority(struct list_elem *e1, struct list_elem *e2, void *aux) {
	return list_entry(e1,struct thread,donation_elem)->priority > list_entry(e2,struct thread, donation_elem)->priority;
}

void donate_priority(void) {

    int depth;
    struct thread *curr = thread_current();

    for (depth = 0; depth < 8 ; depth++) {
        if(!curr->wait_on_lock) 
            break;
        struct thread *holder = curr->wait_on_lock->holder;
        holder->priority = curr->priority;
        curr = holder;
    }
}

void remove_with_lock(struct lock *lock) {
    struct list_elem *e;
    struct thread *curr = thread_current();

    for (e = list_begin(&curr->donations); e != list_end(&curr->donations); e = list_next(e)) {
        struct thread *t = list_entry(e, struct thread, donation_elem);
        if (t->wait_on_lock == lock) 
            list_remove(&t->donation_elem);
    }
}

void refresh_priority(void) {
    struct thread *curr = thread_current();
    curr->priority = curr->init_priority;
    
    if (!list_empty(&curr->donations)) {
        list_sort(&curr->donations, thread_compare_donate_priority, NULL);
        struct thread *front = list_entry(list_front(&curr->donations), struct thread, donation_elem);
        if (front->priority > curr->priority)
            curr->priority = front->priority;
    }
}