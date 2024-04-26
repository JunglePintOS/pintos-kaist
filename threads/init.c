#include "threads/init.h"
#include <console.h>
#include <debug.h>
#include <limits.h>
#include <random.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "devices/kbd.h"
#include "devices/input.h"
#include "devices/serial.h"
#include "devices/timer.h"
#include "devices/vga.h"
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/loader.h"
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "threads/palloc.h"
#include "threads/pte.h"
#include "threads/thread.h"
#ifdef USERPROG
#include "userprog/process.h"
#include "userprog/exception.h"
#include "userprog/gdt.h"
#include "userprog/syscall.h"
#include "userprog/tss.h"
#endif
#include "tests/threads/tests.h"
#ifdef VM
#include "vm/vm.h"
#endif
#ifdef FILESYS
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "filesys/fsutil.h"
#endif

/* 커널 매핑만을 포함하는 페이지 맵 레벨 4 */
/* Page-map-level-4 with kernel mappings only. */
uint64_t *base_pml4;

#ifdef FILESYS
/* -f: 파일 시스템을 포맷할 것인가? */
/* -f: Format the file system? */
static bool format_filesys;
#endif
/* -q: 커널 작업이 완료된 후 시스템을 종료할 것인가? */
/* -q: Power off after kernel tasks complete? */
bool power_off_when_done;
/* 스레드 테스트를 진행할 것인지의 여부 */
bool thread_tests;

/* 선언된 함수들 */
static void bss_init (void);									// BSS 세그먼트를 초기화하는 함수
static void paging_init (uint64_t mem_end);		// 페이징 시스템을 초기화하는 함수

static char **read_command_line (void);				// 커맨드 라인을 읽어오는 함수
static char **parse_options (char **argv);		// 옵션을 파싱하는 함수
static void run_actions (char **argv);				// 주어진 액션을 실행하는 함수
static void usage (void);											// 사용법을 출력하는 함수

static void print_stats (void);								// 통계를 출력하는 함수


int main (void) NO_RETURN;

/* 핀토스 메인 프로그램 */
/* Pintos main program. */
int
main (void) {
	uint64_t mem_end;
	char **argv;

	/* BSS 세그먼트를 초기화하고 시스템 RAM 크기를 가져옵니다. */
	/* Clear BSS and get machine's RAM size. */
	bss_init ();

	/* 커맨드 라인을 인수로 분리하고 옵션을 파싱합니다. */
	/* Break command line into arguments and parse options. */
	argv = read_command_line ();
	argv = parse_options (argv);

	/* 스레드로서 자신을 초기화하여 락을 사용할 수 있게 하고,
	   콘솔 락을 활성화합니다. */
	/* Initialize ourselves as a thread so we can use locks,
	   then enable console locking. */
	thread_init ();
	console_init ();

	/* 메모리 시스템을 초기화합니다. */
	/* Initialize memory system. */
	mem_end = palloc_init ();
	malloc_init ();
	paging_init (mem_end);

#ifdef USERPROG
	tss_init ();
	gdt_init ();
#endif
	/* 인터럽트 핸들러를 초기화합니다. */
	/* Initialize interrupt handlers. */
	intr_init ();
	timer_init ();
	kbd_init ();
	input_init ();
#ifdef USERPROG
	exception_init ();
	syscall_init ();
#endif
	/* 스레드 스케줄러를 시작하고 인터럽트를 활성화합니다. */
	/* Start thread scheduler and enable interrupts. */
	thread_start ();
	serial_init_queue ();
	timer_calibrate ();

#ifdef FILESYS
	/* 파일 시스템을 초기화합니다. */
	/* Initialize file system. */
	disk_init ();
	filesys_init (format_filesys);
#endif

#ifdef VM
	vm_init ();
#endif

	printf ("Boot complete.\n");

	/* 커널 커맨드 라인에 지정된 작업을 실행합니다. */
	/* Run actions specified on kernel command line. */
	run_actions (argv);

	/* 마무리 작업을 수행합니다. */
	/* Finish up. */
	if (power_off_when_done)
		power_off ();
	thread_exit ();
}

/* BSS 영역 초기화 */
/* Clear BSS */
static void
bss_init (void) {
	/* "BSS"는 0으로 초기화되어야 하는 세그먼트입니다.
       이 영역은 디스크에 저장되지 않고 커널 로더에 의해 0으로 초기화되지도 않습니다,
       그래서 우리가 직접 0으로 초기화해야 합니다.

       BSS 세그먼트의 시작과 끝은 링커에 의해 _start_bss와 _end_bss로 기록됩니다.
       kernel.lds를 참고하세요. */
	/* The "BSS" is a segment that should be initialized to zeros.
	   It isn't actually stored on disk or zeroed by the kernel
	   loader, so we have to zero it ourselves.

	   The start and end of the BSS segment is recorded by the
	   linker as _start_bss and _end_bss.  See kernel.lds. */
	extern char _start_bss, _end_bss;
	memset (&_start_bss, 0, &_end_bss - &_start_bss);
}

/* 페이지 테이블을 채워 커널 가상 매핑을 설정하고,
 * 새 페이지 디렉토리를 사용하도록 CPU를 설정합니다.
 * base_pml4는 생성된 pml4를 가리킵니다. */
/* Populates the page table with the kernel virtual mapping,
 * and then sets up the CPU to use the new page directory.
 * Points base_pml4 to the pml4 it creates. */
static void
paging_init (uint64_t mem_end) {
	uint64_t *pml4, *pte;
	int perm;
	pml4 = base_pml4 = palloc_get_page (PAL_ASSERT | PAL_ZERO);

	extern char start, _end_kernel_text;
	// 물리 주소 [0 ~ mem_end]를
	//   [LOADER_KERN_BASE ~ LOADER_KERN_BASE + mem_end]에 매핑합니다.
	// Maps physical address [0 ~ mem_end] to
	//   [LOADER_KERN_BASE ~ LOADER_KERN_BASE + mem_end].
	for (uint64_t pa = 0; pa < mem_end; pa += PGSIZE) {
		uint64_t va = (uint64_t) ptov(pa);

		perm = PTE_P | PTE_W;
		if ((uint64_t) &start <= va && va < (uint64_t) &_end_kernel_text)
			perm &= ~PTE_W;

		if ((pte = pml4e_walk (pml4, va, 1)) != NULL)
			*pte = pa | perm;
	}
	// CR3 레지스터를 새로운 페이지 테이블 주소로 업데이트합니다.
	// reload cr3
	pml4_activate(0);
}

/* 커널 커맨드 라인을 단어로 분리하여 argv 형식의 배열로 반환합니다. */
/* Breaks the kernel command line into words and returns them as
   an argv-like array. */
static char **
read_command_line (void) {
	static char *argv[LOADER_ARGS_LEN / 2 + 1];	// 커맨드 라인 인자를 저장할 배열
	char *p, *end;															// 문자열을 순회하기 위한 포인터들
	int argc;																		// 인자의 수
	int i;																			// 루프 인덱스

	argc = *(uint32_t *) ptov (LOADER_ARG_CNT);	// 커널로부터 전달받은 인자의 수를 읽어옴
	p = ptov (LOADER_ARGS);											// 인자 문자열의 시작 위치
	end = p + LOADER_ARGS_LEN;									// 인자 문자열의 끝 위치
	for (i = 0; i < argc; i++) {								// 인자 수만큼 반복
		if (p >= end)
			PANIC ("command line arguments overflow");	// 인자가 예상 범위를 초과하면 패닉

	argv[i] = p;																// 현재 위치의 문자열을 배열에 저장
		p += strnlen (p, end - p) + 1;						// 다음 인자의 위치로 이동 (null 문자 포함)
	}
	argv[argc] = NULL;													// 배열의 마지막을 NULL로 마킹

	/* 커널 커맨드 라인을 출력합니다. */
	/* Print kernel command line. */
	printf ("Kernel command line:");		// 커맨드 라인 전체 출력 시작
	for (i = 0; i < argc; i++)
		if (strchr (argv[i], ' ') == NULL)
			printf (" %s", argv[i]);				// 공백이 없는 인자는 그대로 출력
		else
			printf (" '%s'", argv[i]);			// 공백이 포함된 인자는 따옴표로 감싸서 출력
	printf ("\n");

	return argv;												// 파싱된 인자 배열 반환
}

/* ARGV 배열에서 옵션을 파싱하고,
   옵션이 아닌 첫 번째 인자를 반환합니다. */
/* Parses options in ARGV[]
   and returns the first non-option argument. */
static char **
parse_options (char **argv) {
	for (; *argv != NULL && **argv == '-'; argv++) {		// 옵션 인자를 순회
		char *save_ptr;																		// strtok_r 함수에 사용될 포인터
		char *name = strtok_r (*argv, "=", &save_ptr);		// 옵션 이름 추출
		char *value = strtok_r (NULL, "", &save_ptr);			// 옵션 값 추출

		if (!strcmp (name, "-h"))													// 도음말 옵션
			usage ();
		else if (!strcmp (name, "-q"))										// 종류 후 시스템 전원 종료 옵션
			power_off_when_done = true;
#ifdef FILESYS
		else if (!strcmp (name, "-f"))										// 파일 시스템 포멧 옵션
			format_filesys = true;
#endif
		else if (!strcmp (name, "-rs"))										// 랜덤 시드 초기화
			random_init (atoi (value));
		else if (!strcmp (name, "-mlfqs"))								// 다중 레벨 피드백 큐 스케줄러 사용 옵션
			thread_mlfqs = true;
#ifdef USERPROG
		else if (!strcmp (name, "-ul"))										// 사용자 페이지 제한 설정
			user_page_limit = atoi (value);
		else if (!strcmp (name, "-threads-tests"))				// 스레드 테스트 실행 옵션
			thread_tests = true;
#endif
		else
			PANIC ("unknown option `%s' (use -h for help)", name);		// 알려지지 않은 옵션 처리
	}

	return argv;																				// 옵션이 아닌 첫 인자를 가리키는 포인터 반환
}

/* ARGV[1]에 지정된 작업을 실행합니다. */
/* Runs the task specified in ARGV[1]. */
static void
run_task (char **argv) {
	const char *task = argv[1];				// 실행할 작업의 이름

	printf ("Executing '%s':\n", task);
#ifdef USERPROG
	if (thread_tests){								// 스레드 테스트가 활성화된 경우
		run_test (task);
	} else {													// 일반 사용자 프로그램 실행
		process_wait (process_create_initd (task));
	}
#else
	run_test (task);									// 스레드 테스트 실행
#endif
	printf ("Execution of '%s' complete.\n", task);
}

/* 지정된 ARGV[]에 있는 모든 작업을 실행합니다.
   NULL 포인터 전까지 실행합니다. */
/* Executes all of the actions specified in ARGV[]
   up to the null pointer sentinel. */
static void
run_actions (char **argv) {
	/* An action. */
	struct action {
		char *name;                       /* 작업 이름. *//* Action name. */
		int argc;                         /* 인자의 수, 작업 이름 포함. *//* # of args, including action name. */
		void (*function) (char **argv);   /* 작업을 실행하는 함수. *//* Function to execute action. */
	};

	/* 지원되는 작업의 테이블. */
	/* Table of supported actions. */
	static const struct action actions[] = {
		{"run", 2, run_task},
#ifdef FILESYS
		{"ls", 1, fsutil_ls},
		{"cat", 2, fsutil_cat},
		{"rm", 2, fsutil_rm},
		{"put", 2, fsutil_put},
		{"get", 2, fsutil_get},
#endif
		{NULL, 0, NULL},
	};

	while (*argv != NULL) {
		const struct action *a;
		int i;

		/* 작업 이름을 찾습니다. */
		/* Find action name. */
		for (a = actions; ; a++)
			if (a->name == NULL)
				PANIC ("unknown action `%s' (use -h for help)", *argv);
			else if (!strcmp (*argv, a->name))
				break;

		/* 필요한 인자의 수를 확인합니다. */
		/* Check for required arguments. */
		for (i = 1; i < a->argc; i++)
			if (argv[i] == NULL)
				PANIC ("action `%s' requires %d argument(s)", *argv, a->argc - 1);

		/* 작업을 호출하고 인자를 전진시킵니다. */
		/* Invoke action and advance. */
		a->function (argv);
		argv += a->argc;
	}

}

/* 커널 커맨드 라인 도움말 메시지를 출력하고 기계를 종료합니다. */
/* Prints a kernel command line help message and powers off the
   machine. */
static void
usage (void) {
	printf ("\nCommand line syntax: [OPTION...] [ACTION...]\n"
			"Options must precede actions.\n"												// 옵션은 액션보다 먼저 와야 합니다.
			"Actions are executed in the order specified.\n"				// 액션은 지정된 순서대로 실행됩니다.
			"\nAvailable actions:\n"																// 사용가능한 액션들
#ifdef USERPROG
			"  run 'PROG [ARG...]' Run PROG and wait for it to complete.\n"
#else
			"  run TEST           Run TEST.\n"											// 테스트 실행
#endif
#ifdef FILESYS
			"  ls                 List files in the root directory.\n"							// 루트 디렉토리 파일 목록 표시
			"  cat FILE           Print FILE to the console.\n"											// 콘솔에 file 출력
			"  rm FILE            Delete FILE.\n"																		// file 삭제
			"Use these actions indirectly via `pintos' -g and -p options:\n"				// 
			"  put FILE           Put FILE into file system from scratch disk.\n"
			"  get FILE           Get FILE from file system into scratch disk.\n"
#endif
			"\nOptions:\n"
			"  -h                 Print this help message and power off.\n"					// 도움말
			"  -q                 Power off VM after actions or on panic.\n"				// 액션 실행 후 또는 패닉 시 VM 종료
			"  -f                 Format file system disk during startup.\n"				// 시작 시 파일 시스템 디스크를 포맷
			"  -rs=SEED           Set random number seed to SEED.\n"								// 난수 시드를 SEED 로 설정
			"  -mlfqs             Use multi-level feedback queue scheduler.\n"			// 멀티 레벨 피드백 큐 스케줄러를 사용합니다.
#ifdef USERPROG
			"  -ul=COUNT          Limit user memory to COUNT pages.\n"							// 사용자 메모리를 count 페이지로 제한
#endif
			);
	power_off ();
}

/* 우리가 사용하는 기계를 종료합니다.
   Bochs 또는 QEMU에서 실행 중인 경우에만 동작합니다. */
/* Powers down the machine we're running on,
   as long as we're running on Bochs or QEMU. */
void
power_off (void) {
#ifdef FILESYS
	filesys_done ();			// 파일 시스템 정리
#endif

	print_stats ();				// 실행 통계 출력

	printf ("Powering off...\n");
	outw (0x604, 0x2000);               /* QEMU를 종료하는 명령 *//* Poweroff command for qemu */
	for (;;);														// 무한루프로 종료 대기
}

/* Pintos 실행에 대한 통계를 출력합니다. */
/* Print statistics about Pintos execution. */
static void
print_stats (void) {
	timer_print_stats ();			// 타이머 통계
	thread_print_stats ();		// 스레드 통계
#ifdef FILESYS
	disk_print_stats ();			// 디스크 통계
#endif
	console_print_stats ();		// 콘솔 통계
	kbd_print_stats ();				// 키보드 통계
#ifdef USERPROG
	exception_print_stats ();	// 예외 통계
#endif
}
