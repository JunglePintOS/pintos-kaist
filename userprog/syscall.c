#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
void halt();
void exit(int status);
bool create(const char *name, off_t initial_size);

/* 시스템 호출.
 *
 * 이전에 시스템 호출 서비스는 인터럽트 핸들러에서 처리되었습니다
 * (예: 리눅스에서 int 0x80). 그러나 x86-64에서는 제조사가
 * 효율적인 시스템 호출 요청 경로를 제공합니다. 바로 `syscall` 명령입니다.
 *
 * syscall 명령은 Model Specific Register (MSR)에서 값을 읽어와서 동작합니다.
 * 자세한 내용은 메뉴얼을 참조하세요. */
/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* 세그먼트 선택자 MSR *//* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target *//* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* eflags용 마스크 *//* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* 인터럽트 서비스 루틴은 syscall_entry가 유저랜드 스택을 커널
	 * 모드 스택으로 전환할 때까지 어떤 인터럽트도 처리해서는 안 됩니다.
	 * 따라서 FLAG_FL을 마스킹했습니다. */
	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* 주요 시스템 호출 인터페이스 */
/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED) {
    // todo: 구현을 여기에 하세요
    // TODO: Your implementation goes here.
    printf("system call!\n");

    int sys_num = f->R.rax;
    printf("sys_num : %d\n",sys_num);

    switch (sys_num)
    {
    case SYS_HALT:
      halt();
      break;
    case SYS_WRITE:
      break;
    case SYS_EXIT:
      exit(f->R.rdi);
      break;
    case SYS_FORK:
      break;
    case SYS_CREATE:
      f->R.rax = create(f->R.rdi, f->R.rsi);
      break;
    default:
      thread_exit();
      break;
    }
    // thread_exit();
}

/* 주소 유효성 검수하는 함수 */
void check_address(void *addr) {
  struct thread *t = thread_current();

  if (addr == NULL || !is_user_vaddr(addr)) // 사용자 영역 주소인지 확인
    exit(-1);
  if (pml4_get_page(t->pml4, addr) == NULL) // 페이지로 할당된 영역인지 확인
    exit(-1);
}

void halt() {
  printf("halt test \n");
  power_off();
}

/* 현재 실행중인 스레드를 종료하는 함수 */
void exit(int status) {
  printf("exit test \n");
  struct thread *t = thread_current();
  thread_exit();
}

bool create(const char *name, off_t initial_size) {
  check_address(name);
  printf("create test : name %s \n", name);
  return filesys_create(name, initial_size);
}


