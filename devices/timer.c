#include "devices/timer.h"

#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>

#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* 하드웨어 세부 사항은 [8254] 문서를 참조하세요. */
/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19	//8254 타이머는 TIMER_FREQ가 최소 19 이상이어야 합니다.
#endif
#if TIMER_FREQ > 1000  // TIMER_FREQ는 1000 이하가 권장됩니다
#error TIMER_FREQ <= 1000 recommended
#endif

/* OS 부팅 이후 타이머 틱의 수 */
/* Number of timer ticks since OS booted. */
static int64_t ticks;

/* 타이머 틱당 루프 수. timer_calibrate() 함수에 의해 초기화됩니다. */
/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops(unsigned loops);
static void busy_wait(int64_t loops);
static void real_time_sleep(int64_t num, int32_t denom);

/* 8254 프로그래머블 인터벌 타이머(PIT)를 설정하여
   초당 PIT_FREQ 회 인터럽트가 발생하도록 하고,
   해당 인터럽트를 등록합니다. */
/* Sets up the 8254 Programmable Interval Timer (PIT) to
   interrupt PIT_FREQ times per second, and registers the
   corresponding interrupt. */
void timer_init(void) {
    /* 8254 입력 주파수를 TIMER_FREQ로 나눈 후, 가장 가까운 값으로 반올림합니다. */
    /* 8254 input frequency divided by TIMER_FREQ, rounded to
       nearest. */
    uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

    outb(0x43, 0x34); /* CW: counter 0, LSB then MSB, mode 2, binary. */ /* CW: 카운터 0, LSB 후 MSB, 모드 2, 이진형식. */
    outb(0x40, count & 0xff);
    outb(0x40, count >> 8);

    intr_register_ext(0x20, timer_interrupt, "8254 Timer");
}

/* 타이머 틱당 루프 수를 교정합니다. 이 값은 짧은 지연을 구현하는 데 사용됩니다. */
/* Calibrates loops_per_tick, used to implement brief delays. */
void timer_calibrate(void) {
    unsigned high_bit, test_bit;

    ASSERT(intr_get_level() == INTR_ON);
    printf("Calibrating timer...  ");

    /* 타이머 틱당 최대 2의 제곱 수를 추정합니다. */
    /* Approximate loops_per_tick as the largest power-of-two
       still less than one timer tick. */
    loops_per_tick = 1u << 10;
    while (!too_many_loops(loops_per_tick << 1)) {
        loops_per_tick <<= 1;
        ASSERT(loops_per_tick != 0);
    }

    /* 다음 8비트의 loops_per_tick을 미세 조정합니다. */
    /* Refine the next 8 bits of loops_per_tick. */
    high_bit = loops_per_tick;
    for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
        if (!too_many_loops(high_bit | test_bit))
            loops_per_tick |= test_bit;

    printf("%'" PRIu64 " loops/s.\n", (uint64_t)loops_per_tick * TIMER_FREQ);
}

/* OS가 부팅된 이후 타이머 틱 수를 반환합니다. */
/* Returns the number of timer ticks since the OS booted. */
int64_t timer_ticks(void) {
    enum intr_level old_level = intr_disable();
    int64_t t = ticks;
    intr_set_level(old_level);
    barrier();
    return t;
}

/* THEN 시점 이후 경과한 타이머 틱 수를 반환합니다. */
/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t timer_elapsed(int64_t then) {
    return timer_ticks() - then;
}

/* 지정된 타이머 틱 수만큼 실행을 일시 중지합니다. */
/* Suspends execution for approximately TICKS timer ticks. */
void timer_sleep(int64_t ticks) {
    int64_t start = timer_ticks();

    ASSERT(intr_get_level() == INTR_ON);
    thread_sleep(start + ticks);
}



/* 지정된 밀리초 수만큼 실행을 일시 중지합니다. */
/* Suspends execution for approximately MS milliseconds. */
void timer_msleep(int64_t ms) {
    real_time_sleep(ms, 1000);
}

/* 지정된 마이크로초 수만큼 실행을 일시 중지합니다. */
/* Suspends execution for approximately US microseconds. */
void timer_usleep(int64_t us) {
    real_time_sleep(us, 1000 * 1000);
}

/* 지정된 나노초 수만큼 실행을 일시 중지합니다. */
/* Suspends execution for approximately NS nanoseconds. */
void timer_nsleep(int64_t ns) {
    real_time_sleep(ns, 1000 * 1000 * 1000);
}

/* 타이머 통계를 출력합니다. */
/* Prints timer statistics. */
void timer_print_stats(void) {
    printf("Timer: %" PRId64 " ticks\n", timer_ticks());
}

/* 타이머 인터럽트 핸들러입니다. */
/* Timer interrupt handler. */
static void timer_interrupt(struct intr_frame *args UNUSED) {
    ticks++;
    thread_tick();

    thread_wakeup(ticks);
}

/* 지정된 루프 반복 횟수가 한 타이머 틱을 초과하는 경우 true를 반환합니다. */
/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool too_many_loops(unsigned loops) {
    /* Wait for a timer tick. */
    int64_t start = ticks;
    while (ticks == start)
        barrier();

    /* Run LOOPS loops. */
    start = ticks;
    busy_wait(loops);

    /* 틱 수가 변경된 경우 너무 오랫동안 반복한 것입니다. */
    /* If the tick count changed, we iterated too long. */
    barrier();
    return start != ticks;
}

/* 구현을 위해 간단한 루프 LOOPS를 반복합니다.
    짧은 지연.

    코드 정렬이 크게 달라질 수 있으므로 NO_INLINE으로 표시했습니다.
    타이밍에 영향을 미치므로 이 함수가 인라인된 경우
    장소마다 다르므로 결과가 어려울 수 있습니다.
    예측하기. */
/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE busy_wait(int64_t loops) {
    while (loops-- > 0)
        barrier();
}

/* 대략적으로 NUM/DENOM 초만큼 실행을 중지합니다. */
/* Sleep for approximately NUM/DENOM seconds. */
static void real_time_sleep(int64_t num, int32_t denom) {
    /* Convert NUM/DENOM seconds into timer ticks, rounding down.

       (NUM / DENOM) s
       ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
       1 s / TIMER_FREQ ticks
       */
    int64_t ticks = num * TIMER_FREQ / denom;

    ASSERT(intr_get_level() == INTR_ON);
    if (ticks > 0) {
        /* 적어도 하나의 전체 타이머 틱을 기다리고 있습니다. 다른 프로세스에 CPU를 양보하기 위해
                timer_sleep()을 사용합니다. */
        /* We're waiting for at least one full timer tick.  Use
           timer_sleep() because it will yield the CPU to other
           processes. */
        timer_sleep(ticks);
    } else {
        /* 그렇지 않으면, 더 정확한 서브-틱 타이밍을 위해 바쁜 대기 루프를 사용합니다.
           오버플로우 가능성을 피하기 위해 분자와 분모를 1000으로 나눕니다. */
        /* Otherwise, use a busy-wait loop for more accurate
           sub-tick timing.  We scale the numerator and denominator
           down by 1000 to avoid the possibility of overflow. */
        ASSERT(denom % 1000 == 0);
        busy_wait(loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
    }
}
