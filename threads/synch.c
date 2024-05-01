/* 이 파일은 Nachos 교육용 운영 체제의 소스 코드에서 파생되었습니다. Nachos의 저작권 고지가 아래에 전체로 재생산됩니다. */

/* 저작권 (c) 1992-1996 캘리포니아 대학 국회 의회. 모든 권리 보유.
	 이 소프트웨어 및 그 문서를 복사, 수정, 배포하는 것을 어떤 목적으로든, 수수료없이 및
	 서면 합의없이 허용됩니다. 위의 저작권 고지와 다음 두 단락이
	 이 소프트웨어의 모든 사본에 나타납니다.
	 캘리포니아 대학은 어떤 당사자에 대해서도 책임을지지 않습니다.
	 이 소프트웨어와 그 문서의 사용으로 발생하는 직접, 간접, 특별, 부수적 또는
	 이후의 손해, 캘리포니아 대학이 그러한 손해의 가능성을 알았더라도
	 면책합니다.
	 캘리포니아 대학은 명시 적으로 부인합니다.
	 상품성 및 특정
	 목적. 여기 제공된 소프트웨어는 "있는 그대로"
	 기초이며, 캘리포니아 대학은 없습니다.
	 유지 보수, 지원, 업데이트, 향상 또는
	 수정. 
*/

/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* 세마포어 SEMA를 VALUE로 초기화합니다. 세마포어는 두 가지 원자 연산을 가진
	 음이 아닌 정수입니다. 이 연산자들은 다음과 같습니다:

	 down 또는 "P": 값이 양수가 될 때까지 기다린 다음,
	 값을 감소시킵니다.
	 up 또는 "V": 값을 증가시킵니다 (그리고 대기 중인 스레드 중 하나를 깨우는 경우가 있습니다). 
*/
/* 이 함수는 세마포어를 초기화하는 데 사용됩니다. 세마포어는 공유 자원에 대한 액세스를 제어하기 위한 동기화 메커니즘으로 사용됩니다.
	 세마포어는 값을 가지며, 이 값은 해당 자원에 대한 사용 가능한 허가 수를 나타냅니다. 초기 값은 주어진 VALUE로 설정됩니다.
	 또한, 세마포어에는 대기 중인 스레드 목록이 있습니다. 이 목록은 세마포어가 현재 사용 중인 자원에 액세스하려는 스레드들을 추적합니다.
*/
/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void sema_init(struct semaphore *sema, unsigned value) {
    ASSERT(sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* 세마포어에 대한 Down 또는 "P" 연산입니다. SEMA의 값이 양수가 될 때까지 기다린 다음 원자적으로 값을 감소시킵니다.

     이 함수는 sleep할 수 있으므로 인터럽트 핸들러 내에서 호출해서는 안됩니다.
     이 함수는 인터럽트가 비활성화된 상태에서 호출될 수 있지만,
     sleep하는 경우 다음에 예정된 스레드가 아마도 인터럽트를 다시 활성화할 것입니다.
     이것이 sema_down 함수입니다.
*/
/* 이 함수는 세마포어의 Down 연산을 수행합니다. 
	 세마포어의 값이 양수가 될 때까지 현재 스레드는 대기 상태가 되며, 대기 목록에 추가됩니다.
	 세마포어의 값이 양수가 될 때까지 기다린 후, 원자적으로 값을 하나 감소시킵니다. 
	 이 함수는 인터럽트를 비활성화한 후에 호출될 수 있지만, 
	 슬립(sleep)하여 대기 중인 경우에는 다음에 예정된 스레드가 인터럽트를 다시 활성화할 수 있습니다.
*/
/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		// list_push_back (&sema->waiters, &thread_current ()->elem);
		list_insert_desc_ordered (&sema->waiters, &thread_current ()->elem, compare_priority, NULL);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* 세마포어에 대한 Down 또는 "P" 연산을 수행하지만, 세마포어가 이미 0이 아닌 경우에만 수행합니다.
     세마포어가 감소되면 true를 반환하고, 그렇지 않으면 false를 반환합니다.

	 이 함수는 인터럽트 핸들러에서 호출될 수 있습니다. */
/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* 세마포어에 대한 Up 또는 "V" 연산을 수행합니다. 
	 SEMA의 값을 증가시키고, SEMA를 기다리는 스레드 중 하나를 깨웁니다.

	 이 함수는 인터럽트 핸들러에서 호출될 수 있습니다. */
/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters))
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
					struct thread, elem));
	sema->value++;
	intr_set_level (old_level);

	test_max_priority();
}

static void sema_test_helper (void *sema_);

/* 세마포어의 셀프 테스트를 수행하는 함수입니다. 이 함수는 두 개의 세마포어를 사용하여 두 개의 스레드 간에 "핑퐁" 동작을 수행합니다.
	 동작 내용을 확인하기 위해 printf() 호출을 삽입할 수 있습니다. */
/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* sema_self_test()에서 사용하는 스레드 함수입니다. */
/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* LOCK을 초기화합니다. 잠금은 언제나 최대 한 스레드에 의해서만 보유될 수 있습니다.
	 우리의 잠금은 "재귀적"이지 않습니다. 즉, 현재 잠금을 보유하고 있는 스레드가 해당 잠금을
	 다시 획득하려고 시도하는 것은 오류이다.

	 잠금은 초기 값이 1인 세마포어의 특수한 경우입니다. 잠금과 이러한 세마포어의 차이점은 두 가지입니다.
	 첫째, 세마포어는 값이 1보다 크게 될 수 있지만, 잠금은 한 번에 하나의 스레드만 소유할 수 있습니다.
	 둘째, 세마포어에는 소유자가 없습니다. 즉, 한 스레드가 세마포어를 "down"한 다음 다른 스레드가 이를 "up"할 수 있지만,
	 잠금의 경우 동일한 스레드가 잠금을 획득하고 해제해야 합니다. 이러한 제한이 불편할 때에는
	 세마포어 대신에 잠금을 사용해야 함을 나타냅니다. */
/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* LOCK을 획득하며, 필요한 경우 사용 가능할 때까지 대기합니다. 현재 스레드가 이미 잠금을 보유하고 있으면 안 됩니다.

     이 함수는 sleep할 수 있으므로 인터럽트 핸들러 내에서 호출해서는 안됩니다.
     이 함수는 인터럽트가 비활성화된 상태에서 호출될 수 있지만, sleep해야 할 경우 인터럽트가 다시 활성화됩니다. */
/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

  struct thread *curr = thread_current();

  if (lock->holder) {
    curr->wait_on_lock = lock;
    list_insert_ordered(&lock->holder->donations, &curr->donation_elem, thread_compare_donate_priority, NULL);
				
		donate_priority();
  }

  sema_down (&lock->semaphore);
  curr->wait_on_lock = NULL;
	lock->holder = curr;
}

/* LOCK을 획득하려고 시도하고, 성공하면 true를 실패하면 false를 반환합니다.
	 현재 스레드가 이미 잠금을 보유하고 있으면 안 됩니다.

	 이 함수는 sleep하지 않으므로 인터럽트 핸들러 내에서 호출될 수 있습니다. */
/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* 현재 스레드가 소유한 LOCK을 해제합니다.
   이것은 lock_release 함수입니다.

   인터럽트 핸들러는 잠금을 획득할 수 없으므로 인터럽트 핸들러 내에서 잠금을 해제하는 것은 의미가 없습니다. */
/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	remove_with_lock(lock);
	refresh_priority();

	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

/* 현재 스레드가 LOCK을 보유하고 있는지 여부를 반환합니다. 
   (다른 스레드가 잠금을 보유하고 있는지 확인하는 것은 경쟁 조건이 발생할 수 있으므로 주의해야 합니다.) */
/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* 리스트에 있는 세마포어 하나를 나타내는 구조체입니다. */
/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* 리스트 요소입니다. *//* List element. */
	struct semaphore semaphore;         /* 이 세마포어입니다. *//* This semaphore. */
};

/* 조건 변수 COND를 초기화합니다. 조건 변수는 한 조각의 코드가 조건을 신호하고, 협력하는
   코드가 그 신호를 받아들이고 그에 따라 작업을 수행할 수 있도록 합니다. */
/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* LOCK을 원자적으로 해제하고, 다른 조각의 코드에 의해 COND가 신호되기를 기다립니다. 
   COND가 신호되면, 반환하기 전에 LOCK을 다시 획득합니다. 이 함수를 호출하기 전에 LOCK을 보유해야 합니다.

   이 함수로 구현된 모니터는 "Mesa" 스타일이며, "Hoare" 스타일이 아닙니다.
   즉, 신호를 보내고 받는 것이 원자적인 작업이 아닙니다. 따라서 대기가 완료된 후에 조건을 다시 확인하고,
   필요한 경우 다시 대기해야 합니다.

   특정 조건 변수는 하나의 잠금에만 관련되어 있지만, 하나의 잠금은 여러 개의 조건 변수에 관련될 수 있습니다.
   즉, 잠금에서 조건 변수로의 일대다 매핑이 있습니다.

   이 함수는 sleep할 수 있으므로 인터럽트 핸들러 내에서 호출해서는 안됩니다. 
   이 함수는 인터럽트가 비활성화된 상태에서 호출될 수 있지만, sleep해야 할 경우 인터럽트가 다시 활성화됩니다. */
/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	// list_push_back (&cond->waiters, &waiter.elem);
	list_insert_desc_ordered(&cond->waiters, &waiter.elem, sema_compare_priority, NULL);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* COND에 대기 중인 스레드가 있다면 (LOCK에 의해 보호됨), 이 함수는 그 중 하나에게 신호를 보내 대기를 깨웁니다.
   이 함수를 호출하기 전에 LOCK을 보유해야 합니다.

   인터럽트 핸들러는 잠금을 획득할 수 없으므로 인터럽트 핸들러 내에서 조건 변수에 신호를 보내려고 하는 것은 의미가 없습니다. */
/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters)) {
		list_sort(&cond->waiters, sema_compare_priority, NULL);
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
	}
}

/* COND (LOCK에 의해 보호됨)에 대기 중인 모든 스레드를 깨웁니다.
   이 함수를 호출하기 전에 LOCK을 보유해야 합니다.

   인터럽트 핸들러는 잠금을 획득할 수 없으므로 인터럽트 핸들러 내에서 조건 변수에 신호를 보내려고 하는 것은 의미가 없습니다. */
/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}

/* 세마포어에서 스레드 꺼내서 우선순위 비교 */
bool
sema_compare_priority (struct list_elem *e1, struct list_elem *e2, void *aux) {
	struct semaphore_elem *e1_sema = list_entry (e1, struct semaphore_elem, elem);
	struct semaphore_elem *e2_sema = list_entry (e2, struct semaphore_elem, elem);

	struct list *waiter_e1_sema = &(e1_sema->semaphore.waiters);
	struct list *waiter_e2_sema = &(e2_sema->semaphore.waiters);

	return list_entry(list_begin (waiter_e1_sema), struct thread, elem)->priority > list_entry (list_begin (waiter_e2_sema), struct thread, elem)->priority;
}