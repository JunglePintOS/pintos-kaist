#ifndef THREADS_VADDR_H
#define THREADS_VADDR_H

#include <debug.h>
#include <stdint.h>
#include <stdbool.h>

#include "threads/loader.h"

/* Functions and macros for working with virtual addresses.
 *
 * See pte.h for functions and macros specifically for x86
 * hardware page tables. */

//비트 마스크를 생성하는 매크로입니다. 특정 비트 위치(SHIFT)부터 특정 개수(CNT)만큼 1이 연속된 비트 마스크를 만듭니다. 페이지 주소 계산에 사용됩니다.
#define BITMASK(SHIFT, CNT) (((1ul << (CNT)) - 1) << (SHIFT)) // 

//페이지 크기와 관련된 상수를 정의합니다. PGSIZE는 페이지의 크기를 바이트 단위로 나타냅니다(4KB). PGMASK는 페이지 내에서의 오프셋을 추출하기 위한 마스크입니다.
/* Page offset (bits 0:12). */
#define PGSHIFT 0                          /* Index of first offset bit. */
#define PGBITS  12                         /* Number of offset bits. */
#define PGSIZE  (1 << PGBITS)              /* Bytes in a page. */
#define PGMASK  BITMASK(PGSHIFT, PGBITS)   /* Page offset bits (0:12). */

/* Offset within a page. */
#define pg_ofs(va) ((uint64_t) (va) & PGMASK) // 가상 주소에서 페이지 내 오프셋을 추출합니다.

#define pg_no(va) ((uint64_t) (va) >> PGBITS) // 가상 주소에서 페이지 번호를 계산합니다.

/* Round up to nearest page boundary. */
#define pg_round_up(va) ((void *) (((uint64_t) (va) + PGSIZE - 1) & ~PGMASK)) // 주어진 가상 주소를 페이지 경계로 올림합니다

/* Round down to nearest page boundary. */
#define pg_round_down(va) (void *) ((uint64_t) (va) & ~PGMASK) // 주어진 가상 주소를 페이지 경계로 내림합니다.

/* Kernel virtual address start */
#define KERN_BASE LOADER_KERN_BASE // 커널의 가상 주소 시작점입니다. LOADER_KERN_BASE는 로더에 의해 정의됩니다.

/* User stack start */
#define USER_STACK 0x47480000 // 사용자 스택의 시작 주소를 정의합니다.

/* Returns true if VADDR is a user virtual address. */
#define is_user_vaddr(vaddr) (!is_kernel_vaddr((vaddr))) // 주어진 가상 주소가 사용자 주소인지, 커널 주소인지 판단하는 매크로입니다.

/* Returns true if VADDR is a kernel virtual address. */
#define is_kernel_vaddr(vaddr) ((uint64_t)(vaddr) >= KERN_BASE)

// FIXME: add checking
/* Returns kernel virtual address at which physical address PADDR
 *  is mapped. */
#define ptov(paddr) ((void *) (((uint64_t) paddr) + KERN_BASE)) // 물리 주소를 커널 가상 주소로 변환합니다.

/* Returns physical address at which kernel virtual address VADDR
 * is mapped. */
#define vtop(vaddr) \
({ \
	ASSERT(is_kernel_vaddr(vaddr)); \
	((uint64_t) (vaddr) - (uint64_t) KERN_BASE);\
}) // 커널 가상 주소를 물리 주소로 변환합니다. 이 변환은 커널 주소 공간에서만 유효합니다.

#endif /* threads/vaddr.h */