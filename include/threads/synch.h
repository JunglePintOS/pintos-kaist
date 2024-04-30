#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* 세마포어입니다. */
/* A counting semaphore. */
struct semaphore {
	unsigned value;             /* 현재 값입니다. *//* Current value. */
	struct list waiters;        /* 대기 중인 스레드의 목록입니다. *//* List of waiting threads. */
};

void sema_init (struct semaphore *, unsigned value);
void sema_down (struct semaphore *);
bool sema_try_down (struct semaphore *);
void sema_up (struct semaphore *);
void sema_self_test (void);

/* 잠금 */
/* Lock. */
struct lock {
	struct thread *holder;      /* 잠금을 보유한 스레드입니다 (디버깅 용). *//* Thread holding lock (for debugging). */
	struct semaphore semaphore; /* 접근을 제어하는 이진 세마포어입니다. *//* Binary semaphore controlling access. */
};

void lock_init (struct lock *);
void lock_acquire (struct lock *);
bool lock_try_acquire (struct lock *);
void lock_release (struct lock *);
bool lock_held_by_current_thread (const struct lock *);

/* 조건 변수입니다. */
/* Condition variable. */
struct condition {
	struct list waiters;        /* 대기 중인 스레드의 목록입니다. *//* List of waiting threads. */
};

void cond_init (struct condition *);
void cond_wait (struct condition *, struct lock *);
void cond_signal (struct condition *, struct lock *);
void cond_broadcast (struct condition *, struct lock *);

/* 우선순위 비교 함수 */
bool sema_compare_priority (struct list_elem *e1, struct list_elem *e2, void *aux);

/* 최적화 바리어입니다.
 *
 * 컴파일러는 최적화 바리어를 통해 연산을 재배열하지 않습니다.
 * 자세한 내용은 참조 가이드의 "최적화 바리어"를 참조하세요. */
/* Optimization barrier.
 *
 * The compiler will not reorder operations across an
 * optimization barrier.  See "Optimization Barriers" in the
 * reference guide for more information.*/
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */
