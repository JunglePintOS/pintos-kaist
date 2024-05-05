#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "intrinsic.h"

/* 페이지 폴트 처리 횟수 */
/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);

/* 사용자 프로그램에 의해 발생할 수 있는 인터럽트에 대한 핸들러를 등록합니다.
   
   실제 Unix와 유사한 운영 체제에서는 이러한 인터럽트 중 대부분을
   신호(signal)의 형태로 사용자 프로세스에 전달할 것입니다. 하지만
   여기서는 신호를 구현하지 않습니다. 대신, 그냥 사용자 프로세스를 종료시킵니다.

   페이지 폴트는 예외입니다. 여기서는 다른 예외와 동일하게 처리되지만,
   가상 메모리를 구현하기 위해 이것은 변경되어야 할 것입니다.

   각 예외에 대한 설명은 [IA32-v3a] 섹션 5.15 "예외 및 인터럽트 참조"를 참조하세요. */
/* Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user
   process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void
exception_init (void) {
	/* 이 예외들은 사용자 프로그램에 의해 명시적으로 발생할 수 있습니다.
	   예를 들어, INT, INT3, INTO, BOUND 명령어를 통해.
	   따라서 DPL==3으로 설정하여 사용자 프로그램이 이러한 명령어를 통해
	   이들을 호출할 수 있도록 합니다. */
	/* These exceptions can be raised explicitly by a user program,
	   e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
	   we set DPL==3, meaning that user programs are allowed to
	   invoke them via these instructions. */
	intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
	intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
	intr_register_int (5, 3, INTR_ON, kill,
			"#BR BOUND Range Exceeded Exception");

	/* 이 예외들은 DPL==0으로 설정되어 있어 사용자 프로세스가
	   INT 명령어를 통해 이들을 호출하는 것을 방지합니다.
	   하지만 간접적으로 발생될 수 있습니다. 예를 들어 #DE는
	   0으로 나누면 발생할 수 있습니다. */
	/* These exceptions have DPL==0, preventing user processes from
	   invoking them via the INT instruction.  They can still be
	   caused indirectly, e.g. #DE can be caused by dividing by
	   0.  */
	intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
	intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
	intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
	intr_register_int (7, 0, INTR_ON, kill,
			"#NM Device Not Available Exception");
	intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
	intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
	intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
	intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
	intr_register_int (19, 0, INTR_ON, kill,
			"#XF SIMD Floating-Point Exception");

	/* 대부분의 예외는 인터럽트가 활성화된 상태에서 처리될 수 있습니다.
	   페이지 폴트는 CR2에 주소가 저장되어야 하므로 인터럽트를 비활성화해야 합니다. */
	/* Most exceptions can be handled with interrupts turned on.
	   We need to disable interrupts for page faults because the
	   fault address is stored in CR2 and needs to be preserved. */
	intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* 예외 통계를 출력합니다. */
/* Prints exception statistics. */
void
exception_print_stats (void) {
	printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* 사용자 프로세스에 의해 발생한 예외를 처리하는 핸들러입니다. */
/* Handler for an exception (probably) caused by a user process. */
static void
kill (struct intr_frame *f) {
	/* 이 인터럽트는 사용자 프로세스에 의해 발생했습니다 (아마도).
	   예를 들어, 프로세스가 매핑되지 않은 가상 메모리에 액세스하려고 시도했을 수 있습니다 (페이지 폴트).
	   현재는 그냥 사용자 프로세스를 종료합니다. 나중에는 페이지 폴트를 커널에서 처리해야 합니다.
	   실제 Unix와 유사한 운영 체제에서는 대부분의 예외를 신호(signal)를 통해 프로세스에게 돌려줍니다만,
	   여기서는 그렇게 구현하지 않습니다. */
	/* This interrupt is one (probably) caused by a user process.
	   For example, the process might have tried to access unmapped
	   virtual memory (a page fault).  For now, we simply kill the
	   user process.  Later, we'll want to handle page faults in
	   the kernel.  Real Unix-like operating systems pass most
	   exceptions back to the process via signals, but we don't
	   implement them. */

	/* 인터럽트 프레임의 코드 세그먼트 값은 예외가 발생한 위치를 알려줍니다. */
	/* The interrupt frame's code segment value tells us where the
	   exception originated. */
	switch (f->cs) {
		case SEL_UCSEG:
			/* 사용자 코드 세그먼트이므로, 사용자 예외입니다. 사용자 프로세스를 종료합니다. */
			/* User's code segment, so it's a user exception, as we
			   expected.  Kill the user process.  */
			printf ("%s: dying due to interrupt %#04llx (%s).\n",
					thread_name (), f->vec_no, intr_name (f->vec_no));
			intr_dump_frame (f);
			thread_exit ();

		case SEL_KCSEG:
		/* 커널 코드 세그먼트는 커널 버그를 나타냅니다.
			   커널 코드는 예외를 발생시키지 않아야 합니다. (페이지 폴트는
			   커널 예외를 일으킬 수 있지만, 여기까지 도달해서는 안됩니다.) */
			/* Kernel's code segment, which indicates a kernel bug.
			   Kernel code shouldn't throw exceptions.  (Page faults
			   may cause kernel exceptions--but they shouldn't arrive
			   here.)  Panic the kernel to make the point.  */
			intr_dump_frame (f);
			PANIC ("Kernel bug - unexpected interrupt in kernel");

		default:
			/* 다른 코드 세그먼트? 이런 일이 있어서는 안 됩니다. 커널을 종료합니다. */
			/* Some other code segment?  Shouldn't happen.  Panic the
			   kernel. */
			printf ("Interrupt %#04llx (%s) in unknown segment %04x\n",
					f->vec_no, intr_name (f->vec_no), f->cs);
			thread_exit ();
	}
}

/* 페이지 폴트 핸들러입니다.
   
   가상 메모리를 구현하기 위해 이것을 채워 넣어야 합니다.
   프로젝트 2의 일부 솔루션은 이 코드를 수정해야 할 수도 있습니다.

   입구에서, faulted된 주소가 CR2(Control Register 2)에 있으며
   F의 error_code 멤버에는 예외에 대한 정보가 포함되어 있습니다.
   이 정보의 형식은 exception.h의 PF_* 매크로에 설명되어 있습니다. */
/* Page fault handler.  This is a skeleton that must be filled in
   to implement virtual memory.  Some solutions to project 2 may
   also require modifying this code.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference". */
static void
page_fault (struct intr_frame *f) {
	bool not_present;  /* True: not-present page, false: writing r/o page. */
	bool write;        /* True: access was write, false: access was read. */
	bool user;         /* True: access by user, false: access by kernel. */
	void *fault_addr;  /* Fault address. */

	/* 폴트 발생 주소를 얻습니다. 이는 폴트를 일으킨 가상 주소입니다.
	   이는 코드나 데이터를 가리킬 수 있습니다.
	   반드시 폴트를 일으킨 명령어의 주소가 아닐 수 있습니다 (그것은 f->rip입니다). */
	/* Obtain faulting address, the virtual address that was
	   accessed to cause the fault.  It may point to code or to
	   data.  It is not necessarily the address of the instruction
	   that caused the fault (that's f->rip). */

	fault_addr = (void *) rcr2();

	/* 인터럽트를 다시 켭니다 (CR2가 변경되기 전에 확실하게 읽을 수 있도록). */
	/* Turn interrupts back on (they were only off so that we could
	   be assured of reading CR2 before it changed). */
	intr_enable ();

	/* 원인을 결정합니다. */
	/* Determine cause. */
	not_present = (f->error_code & PF_P) == 0;
	write = (f->error_code & PF_W) != 0;
	user = (f->error_code & PF_U) != 0;

#ifdef VM
	/* 프로젝트 3부터 사용됩니다. */
	/* For project 3 and later. */
	if (vm_try_handle_fault (f, fault_addr, user, write, not_present))
		return;
#endif
	/* 페이지 폴트 횟수를 셉니다. */
	/* Count page faults. */
	page_fault_cnt++;

	/* 폴트가 진짜 폴트인 경우 정보를 표시하고 종료합니다. */
	/* If the fault is true fault, show info and exit. */
	printf ("Page fault at %p: %s error %s page in %s context.\n",
			fault_addr,
			not_present ? "not present" : "rights violation",
			write ? "writing" : "reading",
			user ? "user" : "kernel");
	kill (f);
}

