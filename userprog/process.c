#include "userprog/process.h"
#define USERPROG
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "threads/synch.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "intrinsic.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/mmu.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/gdt.h"
#include "userprog/tss.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup(void);
static bool load(const char *file_name, struct intr_frame *if_);
static void initd(void *f_name);
static void __do_fork(void *);
struct thread *get_child_with_pid(tid_t tid);

/* initd와 다른 프로세스를 위한 일반 프로세스 초기화 함수 */
/* General process initializer for initd and other process. */
static void process_init(void) {
    struct thread *current = thread_current();
}

/* 함수는 주의 깊게 한 번만 호출
 * 첫 번째 사용자 레벨 프로그램(일반적으로 초기화 데몬을 의미하는 "initd")을 시작하는 함수
 * 특정 파일 이름(FILE_NAME)을 받아 해당 프로그램을 실행하는 새로운 스레드를 생성 */
/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t process_create_initd(const char *file_name) {
    char *fn_copy;
    tid_t tid;

    // 먼저 palloc_get_page(0)를 호출하여 페이지 할당자로부터 메모리 페이지를 할당받아 파일 이름을 복사할 공간(fn_copy)을 확보합니다.
    // 이는 load() 함수와의 경쟁 상태를 방지하기 위함입니다. load() 함수가 실행 파일을 메모리로 로드하는 동안,
    // 원본 파일 이름이 변경되거나 손상되는 것을 막기 위해 파일 이름의 복사본을 만드는 것
    /* Make a copy of FILE_NAME.
     * Otherwise there's a race between the caller and load(). */
    fn_copy = palloc_get_page(0);
    if (fn_copy == NULL)
        return TID_ERROR;
    strlcpy(fn_copy, file_name, PGSIZE);  // 4096 바이트(4KB)

    // thread_create 함수를 호출하여 새로운 스레드를 생성합니다.
    // 이 함수는 실행할 파일의 이름(file_name), 스레드의 우선순위(PRI_DEFAULT),
    // 스레드가 시작할 때 실행할 함수(initd), 그리고 initd 함수에 전달할 인자(fn_copy)를 매개변수로 받습니다.
    // 즉, 이 새로운 스레드는 initd 함수를 실행하게 되며, 이 함수는 파일 이름의 복사본을 사용하여 해당 파일을 로드하고 실행
    /* Create a new thread to execute FILE_NAME. */
    tid = thread_create(file_name, PRI_DEFAULT, initd, fn_copy);
    // 스레드 생성에 실패할 경우(TID_ERROR), 할당받았던 메모리 페이지(fn_copy)를 해제합니다. 이는 메모리 누수를 방지
    if (tid == TID_ERROR)
        palloc_free_page(fn_copy);
    // 성공적으로 스레드를 생성했다면, 생성된 스레드의 ID(tid)를 반환
    return tid;
}

// 첫 번째 사용자 프로세스를 시작하는 스레드 함수인 initd를 정의
/* A thread function that launches first user process. */
static void initd(void *f_name) {
// #ifdef VM부터 #endif까지의 코드는 Pintos의 가상 메모리 관리 기능이 활성화되어 있을 때만 실행됩니다.
#ifdef VM
    //  현재 스레드의 보조 페이지 테이블을 초기화합니다.
    // 이는 가상 메모리 관리에 사용되며, 페이지 폴트 발생 시 필요한 페이지를 찾기 위해 사용됩니다.
    supplemental_page_table_init(&thread_current()->spt);
#endif

    // 필요한 프로세스 관리 구조를 초기화
    process_init();

    // f_name에 지정된 이름의 파일을 실행 파일로 로드하고 실행합니다. 이 함수는 성공 시 0 이상의 값을, 실패 시 -1을 반환합니다.
    if (process_exec(f_name) < 0)
        PANIC("Fail to launch initd\n");
    // process_exec가 성공적으로 실행되면 해당 프로세스 내에서 계속 실행되므로, NOT_REACHED() 아래의 코드가 실행될 일이 없습니다.
    NOT_REACHED();
}

// 현재 프로세스의 자식리스트를 검색하여 해당 tid에 맞는 디스크립터 반환
struct thread *get_child_with_pid(tid_t tid) {
    struct thread *parent = thread_current();
    struct list_elem *e;
    
    for ( e = list_begin(&parent->child_list); e != list_end(&parent->child_list); e = list_next(e)) {
        struct thread *child = list_entry(e, struct thread, child_elem);
        if ( child->tid == tid ){
            return child;
        }
    }
    return NULL;
}

//  현재 프로세스(스레드)를 복제하여 새로운 프로세스(스레드)를 생성하고,
// 생성된 새 프로세스의 스레드 ID를 반환합니다. 만약 스레드 생성에 실패할 경우 TID_ERROR를 반환
/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t process_fork(const char *name, struct intr_frame *if_ UNUSED) {
    /* Clone current thread to new thread.*/

    struct thread *curr = thread_current();

    tid_t tid = thread_create(name, PRI_DEFAULT, __do_fork, curr);
    if(tid == TID_ERROR)
        return TID_ERROR;

    struct thread *child = get_child_with_pid(tid); // child_list안에서 만들어진 child thread를 찾음
    sema_down(&child -> fork_sema); // 자식이 메모리에 load될 때까지 기다림(blocked)
    if (child -> exit_status == -1)
        return TID_ERROR;
    
    return tid;
}

#ifndef VM

/* 부모의 주소 공간을 복제하기 위해 이 함수를 pml4_for_each에 전달합니다.
 * 이것은 프로젝트 2 전용입니다. */
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool duplicate_pte(uint64_t *pte, void *va, void *aux) {
    struct thread *current = thread_current();
    struct thread *parent = (struct thread *)aux;
    void *parent_page;
    void *newpage;
    bool writable;

    /* 1. TODO: 부모 페이지가 커널 페이지인 경우 즉시 반환합니다. */
    /* 1. TODO: If the parent_page is kernel page, then return immediately. */
    if (is_kernel_vaddr(va))
        return true;
    /* 2. 부모의 페이지 맵 레벨 4에서 VA를 해결합니다. */
    /* 2. Resolve VA from the parent's page map level 4. */
    parent_page = pml4_get_page(parent->pml4, va);
    if (parent_page == NULL) {
        return false;
    }
    /* 3. TODO: 자식을 위해 새로운 PAL_USER 페이지를 할당하고 결과를
     *    TODO: NEWPAGE에 설정합니다. */
    /* 3. TODO: Allocate new PAL_USER page for the child and set result to
     *    TODO: NEWPAGE. */
    newpage = palloc_get_page(PAL_USER);
    if (newpage == NULL) {
        return false;
    }
    
    /* 4. TODO: 부모의 페이지를 새 페이지로 복제하고
     *    TODO: 부모의 페이지가 쓰기 가능한지 여부를 확인합니다 (결과에 따라 WRITABLE을 설정합니다). */
    /* 4. TODO: Duplicate parent's page to the new page and
     *    TODO: check whether parent's page is writable or not (set WRITABLE
     *    TODO: according to the result). */
    memcpy(newpage, parent_page, PGSIZE);
    writable = is_writable(pte);
    /* 5. 새 페이지를 주소 VA에 WRITABLE 권한으로 자식의 페이지 테이블에 추가합니다. */
    /* 5. Add new page to child's page table at address VA with WRITABLE
     *    permission. */
    if (!pml4_set_page(current->pml4, va, newpage, writable)) {
        /* 6. TODO: 페이지 삽입에 실패하면 오류 처리를 수행합니다. */
        /* 6. TODO: if fail to insert page, do error handling. */
        return false;
    }
    return true;
}
#endif

/* 부모의 실행 컨텍스트를 복사하는 스레드 함수입니다.
 * 팁) parent->tf는 프로세스의 유저 랜드 컨텍스트를 유지하지 않습니다.
 *     즉, 이 함수에 process_fork의 두 번째 인수를 전달해야 합니다. */
/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void __do_fork(void *aux) {
    struct intr_frame if_;
    struct thread *parent = (struct thread *)aux;
    struct thread *current = thread_current();
    /* TODO: 부모 if_를 어떻게 전달할 지 고민합니다. (즉, process_fork()의 if_를) */
    /* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
    struct intr_frame *parent_if = &parent -> parent_if;
    bool succ = true;

    /* 1. CPU 컨텍스트를 로컬 스택으로 읽어옵니다. */
    /* 1. Read the cpu context to local stack. */
    memcpy(&if_, parent_if, sizeof(struct intr_frame));
    if_.R.rax = 0;

    /* 2. PT 복제 */
    /* 2. Duplicate PT */
    current->pml4 = pml4_create();
    if (current->pml4 == NULL){
        succ = false;
        goto error;
    }

    process_activate(current);
#ifdef VM
    supplemental_page_table_init(&current->spt);
    if (!supplemental_page_table_copy(&current->spt, &parent->spt)) {
        succ = false;
        goto error;
    }
#else
    if (!pml4_for_each(parent->pml4, duplicate_pte, parent)) {
        succ = false;
        goto error;
    }
#endif

    /* TODO: 여기에 코드를 작성합니다.
     * TODO: 힌트) 파일 객체를 복제하려면 include/filesys/file.h의 `file_duplicate`를 사용하세요.
     * TODO:       부모가 fork()에서 반환되지 않아야만 이 함수가 부모의 리소스를 성공적으로 복제합니다. */
    /* TODO: Your code goes here.
     * TODO: Hint) To duplicate the file object, use `file_duplicate`
     * TODO:       in include/filesys/file.h. Note that parent should not return
     * TODO:       from the fork() until this function successfully duplicates
     * TODO:       the resources of parent.*/
    process_init();  // 굳이 초기화할 필요가 없어서 일단 주석

    // if (parent->fd_idx == FDT_COUNT_LIMIT)
    //     goto error;
    
    /* 파일 디스크립터 테이블 복제 */
    for (int fd = 0; fd < FDT_COUNT_LIMIT; fd++) {
        struct file *file = parent->fdt[fd];
        if (file == NULL)
            continue;
  
        struct file *new_file;
        if (file > 2)
            new_file = file_duplicate(file);
        else
            new_file = file;
        current->fdt[fd] = new_file;
        
    }
    current->fd_idx = parent->fd_idx;

    /* 마지막으로, 새롭게 생성된 프로세스로 전환합니다. */
    /* Finally, switch to the newly created process. */
    if (succ)
        sema_up(&current->fork_sema);
        do_iret(&if_);
error:
    current->exit_status = -1;
    sema_up(&current->fork_sema);
    thread_exit();
}

/* 현재 실행 컨텍스트를 f_name으로 전환합니다.
 * 실패 시 -1을 반환합니다. */
/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int process_exec(void *f_name) {
    char *file_name = f_name;
    bool success;

    char *parse[64];
    char *token, *save_ptr;
    int count = 0;
    // cleanup 에서 메모리 공간도 해제될 가능성이 있어서 실행파일이름을 별도의 메모리 공간에 복사하기 위함
    char *fn_copy = palloc_get_page(PAL_ASSERT | PAL_ZERO);
    if (fn_copy == NULL)
        return -1;
    strlcpy(fn_copy, f_name, PGSIZE); 

    for (token = strtok_r(fn_copy, " ", &save_ptr); token != NULL; token = strtok_r(NULL, " ", &save_ptr))
        parse[count++] = token;

    /* thread 구조체의 intr_frame을 사용할 수 없습니다.
     * 이는 현재 스레드가 재스케줄링되면 실행 정보를 멤버에 저장하기 때문입니다. */
    /* We cannot use the intr_frame in the thread structure.
     * This is because when current thread rescheduled,
     * it stores the execution information to the member. */
    struct intr_frame _if;
    _if.ds = _if.es = _if.ss = SEL_UDSEG;
    _if.cs = SEL_UCSEG;
    _if.eflags = FLAG_IF | FLAG_MBS;

    /* 우선 현재 컨텍스트를 종료합니다. */
    /* We first kill the current context */
    process_cleanup();

    /* 그리고 이진 파일을 로드합니다. */
    /* And then load the binary */
    success = load(fn_copy, &_if);

    if (!success)
        return -1;
        
    argument_stack(parse, count, &_if);  // 프로그램 이름과 인자가 저장되어 있는 메모리 공간, count: 인자의 개수, rsp: 스택 포인터를 가리키는 주소
    // hex_dump(_if.rsp, _if.rsp, USER_STACK - _if.rsp, true);

    /* 로드에 실패하면 종료합니다. */
    /* If load failed, quit. */
    palloc_free_page(fn_copy);

    /* 전환된 프로세스를 시작합니다. */
    /* Start switched process. */
    do_iret(&_if);
    NOT_REACHED();
}

void argument_stack(char **parse, int count, struct intr_frame *tf) {
    int total = 0;
    uint64_t addr[count];
    /* 인자 끝부터 함수이름까지 스택에 저장 */
    for (int i = count - 1; i >= 0; i--) {
        total += strlen(parse[i]) + 1;
        tf->rsp = tf->rsp - strlen(parse[i]) - 1;
        memcpy(tf->rsp, parse[i], strlen(parse[i]) + 1);
        addr[i] = tf->rsp;
    }

    /* 인자들 패딩 넣기 (더블워드정렬) */
    int padding = 0;
    int remainder = total % 8;
    if (remainder != 0) {  // 8의 배수가 아니라면
        padding = 8 - remainder;
    }

    // 출력용
    // printf("total: %d\n",total);
    // printf("padding : %d\n",padding);

    tf->rsp = tf->rsp - padding;
    memset(tf->rsp, 0, padding);
    tf->rsp = tf->rsp - 8;
    memset(tf->rsp, 0, 8);

    /* 포인터 인자+1개 담기 */
    for (int i = count - 1; i >= 0; i--) {
        tf->rsp = tf->rsp - 8;
        // printf("addr 주소 : %p\n",&addr[i]);
        memcpy(tf->rsp, &addr[i], 8);
    }
    tf->rsp = tf->rsp - 8;
    memset(tf->rsp, 0, 8);

    // hex_dump(tf->rsp, tf->rsp, USER_STACK - tf->rsp, true);

    // rdi, rsi 값 넣기
    tf->R.rdi = count;
    tf->R.rsi = tf->rsp + 8;

    // printf("rsi : %p\n", tf->R.rsi);
}

/* 스레드 TID가 종료될 때까지 기다리고 종료 상태를 반환합니다.
 * 커널에 의해(즉, 예외로 인해 종료된 경우) 종료된 경우 -1을 반환합니다.
 * TID가 유효하지 않거나 호출 프로세스의 자식이 아닌 경우 또는
 * 주어진 TID에 대해 process_wait()가 이미 성공적으로 호출된 경우,
 * 즉시 -1을 반환하고 대기하지 않습니다.
 *
 * 이 함수는 문제 2-2에서 구현될 것입니다. 현재는 아무 작업도 수행하지 않습니다. */
/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int process_wait(tid_t child_tid UNUSED) {
    /* XXX: 힌트) pintos는 process_wait (initd)를 실행하면 종료합니다. 
     * XXX: 여기에 process_wait를 구현하기 전에 무한 루프를 추가하는 것을 권장합니다. */
    /* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
     * XXX:       to add infinite loop here before
     * XXX:       implementing the process_wait. */


    struct thread *t = thread_current();
    struct thread *child = get_child_with_pid(child_tid);

    if(child == NULL)
        return -1;

    sema_down(&child -> wait_sema);   // sema down (wait sema)- wait 리스트에 부모 프로세스 추가 (자식 프로세스가 종료 될 때까지)
    int exit_status = child -> exit_status;   // 자식 프로세스의 종료를 알림 
    list_remove(&child -> child_elem);  // wait list 에서 remove 
    sema_up(&child -> free_sema);     // sema_up (free_sema) 

    return exit_status;   // 종료가 되면 그 프로세스가 exit 함수로 전달해준 상태(exit status)를 반환

    // 커널에 의해서 종료된다면 (e.g exception에 의해서 죽는 경우 : OOM , segmentation fault 등 ) -1 리턴
}

/* 프로세스를 종료합니다. 이 함수는 thread_exit()에 의해 호출됩니다. */
/* 프로세스를 종료해야 할 시점에서 열린 파일을 모두 close */
/* Exit the process. This function is called by thread_exit (). */
void process_exit(void) {
    struct thread *curr = thread_current();
    /* TODO: 여기에 코드를 작성하세요.
     * TODO: 프로세스 종료 메시지를 구현하세요 (참조:페이지 디렉터리를 파괴하고
     * TODO: project2/process_termination.html).
     * TODO: 여기서 프로세스 자원 정리를 구현하는 것이 좋습니다. */
    /* TODO: Your code goes here.
     * TODO: Implement process termination message (see
     * TODO: project2/process_termination.html).
     * TODO: We recommend you to implement process resource cleanup here. */


    // 자식 프로세스의 종료를 대기 중인 부모 프로세스에게 알림 (세마포어 이용)   
    // 유저 프로세스가 종료되면 부모 프로세스 대기 상태 이탈 후 진행  
    // 프로세스 디스크립터에 프로세스 종료를 알림 (종료 플래그 설정)
    for (int fd = 0; fd < FDT_COUNT_LIMIT; fd++){
        close(fd);
    }
    // 메모리 누수 방지
    palloc_free_page(curr->fdt);
    // 실행중에 수정 못하도록
    file_close(curr->running);

    sema_up(&curr->wait_sema); // 기다리고 있는 부모 thread에게 signal 보냄
    sema_down(&curr->free_sema); // 부모의 exit_status가 정확히 전달되었는지 확인

    // 추후 프로세스 종료 메시지 구현할 것
    process_cleanup();
}

/* 현재 프로세스의 리소스를 해제합니다. */
/* Free the current process's resources. */
static void process_cleanup(void) {
    struct thread *curr = thread_current();

#ifdef VM
    supplemental_page_table_kill(&curr->spt);
#endif

    uint64_t *pml4;
    /* 현재 프로세스의  커널 전용 페이지 디렉터리로 다시 전환합니다. */
    /* Destroy the current process's page directory and switch back
     * to the kernel-only page directory. */
    pml4 = curr->pml4;
    if (pml4 != NULL) {
        /* 여기서 올바른 순서가 중요합니다. 프로세스 페이지 디렉터리로 다시 전환하기 전에
         * cur->pagedir을 NULL로 설정해야 합니다.
         * 이렇게 하지 않으면 타이머 인터럽트가 프로세스 페이지 디렉터리로 다시 전환할 수 있습니다.
         * 프로세스 페이지 디렉터리를 파괴하기 전에 기본 페이지 디렉터리를 활성화해야 합니다.
         * 그렇지 않으면 활성 페이지 디렉터리는 해제된(및 초기화된) 페이지 디렉터리가 될 것입니다. */
        /* Correct ordering here is crucial.  We must set
         * cur->pagedir to NULL before switching page directories,
         * so that a timer interrupt can't switch back to the
         * process page directory.  We must activate the base page
         * directory before destroying the process's page
         * directory, or our active page directory will be one
         * that's been freed (and cleared). */
        curr->pml4 = NULL;
        pml4_activate(NULL);
        pml4_destroy(pml4);
    }
}

/* 다음 스레드에서 사용자 코드를 실행하기 위해 CPU를 설정합니다.
 * 이 함수는 모든 문맥 전환에서 호출됩니다. */
/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void process_activate(struct thread *next) {
    /* 스레드의 페이지 테이블을 활성화합니다. */
    /* Activate thread's page tables. */
    pml4_activate(next->pml4);

    /* 인터럽트 처리에 사용할 스레드의 커널 스택을 설정합니다. */
    /* Set thread's kernel stack for use in processing interrupts. */
    tss_update(next);
}

/* ELF 이진 파일을 로드합니다. 아래 정의들은 ELF 명세서, [ELF1]에서
 * 거의 그대로 가져온 것입니다. */
/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF 타입들. [ELF1] 1-2를 참조하세요. */
/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL 0           /* 무시됩니다. *//* Ignore. */
#define PT_LOAD 1           /* 로드 가능 세그먼트입니다. *//* Loadable segment. */
#define PT_DYNAMIC 2        /* 동적 링킹 정보입니다. *//* Dynamic linking info. */
#define PT_INTERP 3         /* 동적 로더의 이름입니다. *//* Name of dynamic loader. */
#define PT_NOTE 4           /* 보조 정보입니다. *//* Auxiliary info. */
#define PT_SHLIB 5          /* 예약됩니다. *//* Reserved. */
#define PT_PHDR 6           /* 프로그램 헤더 테이블입니다. *//* Program header table. */
#define PT_STACK 0x6474e551 /* 스택 세그먼트입니다. *//* Stack segment. */

#define PF_X 1 /* 실행 가능합니다. *//* Executable. */
#define PF_W 2 /* 쓰기 가능합니다. *//* Writable. */
#define PF_R 4 /* 읽을 수 있습니다. *//* Readable. */

/* 실행 가능한 헤더입니다. [ELF1] 1-4에서 1-8을 참조하세요.
 * 이는 ELF 바이너리의 맨 처음에 나타납니다. */
/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {                     // 실행파일을 위한 파일 형식
    unsigned char e_ident[EI_NIDENT];  // 오프셋 0X00 ~ 0X09
    uint16_t e_type;                   // 0X10 1, 2, 3, 4 는 각각 재배치, 실행, 공유, 코어를 명시 [2바이트]
    uint16_t e_machine;                // 0X12 대상 명령어 집합을 명시 [2바이트]
    uint32_t e_version;                // 0X14 오리지널 ELF인 경우 1로 설정된다.[4바이트]
    uint64_t e_entry;                  // 0X18 엔트리 포인트의 메모리 주소, 프로세스가 어디서 실행을 시작하는지를 말한다. [8바이트]
    uint64_t e_phoff;                  // 0X20 프로그램 헤더 테이블의 시작을 가리킨다. [8바이트]
    uint64_t e_shoff;                  // 0X28 섹션 헤더 테이블의 시작을 가리킨다. [8바이트]
    uint32_t e_flags;                  // 0x30	e_flags	대상 아키텍처에 따라 이 필드의 해석이 달라진다. [4바이트]
    uint16_t e_ehsize;     // 0x34 e_ehsize	이 헤더의 크기를 가지며 일반적으로 64비트의 경우 64바이트, 32비트의 경우 52바이트이다. [2바이트]
    uint16_t e_phentsize;  // 0x36	e_phentsize	프로그램 헤더 테이블 엔트리의 크기를 갖는다. [2바이트]
    uint16_t e_phnum;      // 0x38	e_phnum	프로그램 헤더 테이블에서 엔트리의 개수. [2바이트]
    uint16_t e_shentsize;  // 0x3A	2	e_shentsize	섹션 헤더 테이블 엔트리의 크기를 갖는다. [2바이트]
    uint16_t e_shnum;      // 0x3C	2	e_shnum	섹션 헤더 테이블에서 엔트리의 개수. [2바이트]
    uint16_t e_shstrndx;   // 0x3E e_shstrndx	섹션 이름들을 포함하는 섹션 헤더 테이블 엔트리의 인덱스. [2바이트]
};

struct ELF64_PHDR {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

/* 약어 */
/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack(struct intr_frame *if_);
static bool validate_segment(const struct Phdr *, struct file *);
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage, uint32_t read_bytes, uint32_t zero_bytes, bool writable);

/* ELF 바이너리를 FILE_NAME에서 현재 스레드로 로드합니다.
 * 실행 가능한 진입점을 *RIP에 저장하고
 * 초기 스택 포인터를 *RSP에 저장합니다.
 * 성공하면 true를 반환하고, 그렇지 않으면 false를 반환합니다. */
/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool load(const char *file_name, struct intr_frame *if_) {
    process_init();
    struct thread *t = thread_current();
    struct ELF ehdr;
    struct file *file = NULL;
    off_t file_ofs;
    bool success = false;
    int i;

    /* 페이지 디렉터리를 할당하고 활성화합니다. */
    /* Allocate and activate page directory. */
    t->pml4 = pml4_create(); // 페이지 디렉토리 생성
    if (t->pml4 == NULL)
        goto done;
    process_activate(thread_current()); // 페이지 테이블 활성화

    /* (프로그램 파일) 실행 파일을 엽니다. */
    /* Open executable file. */
    
    lock_acquire(&filesys_lock);
    file = filesys_open(file_name);
    if (file == NULL) {
        lock_release(&filesys_lock);
        printf("load: %s: open failed\n", file_name);
        goto done;
    }
    t -> running = file_reopen(file);
    file_deny_write(t->running);
    lock_release(&filesys_lock);

    /* 실행 가능한 헤더를 읽고 확인합니다. */
    /* Read and verify executable header. */
    if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr || memcmp(ehdr.e_ident, "\177ELF\2\1\1", 7) || ehdr.e_type != 2 || ehdr.e_machine != 0x3E  // amd64
        || ehdr.e_version != 1 || ehdr.e_phentsize != sizeof(struct Phdr) || ehdr.e_phnum > 1024) {
        printf("load: %s: error loading executable\n", file_name);
        goto done;
    }

    /* 프로그램 헤더를 읽습니다. */
    /* Read program headers. */
    file_ofs = ehdr.e_phoff;
    for (i = 0; i < ehdr.e_phnum; i++) {
        struct Phdr phdr;

        if (file_ofs < 0 || file_ofs > file_length(file))
            goto done;
        file_seek(file, file_ofs);

        if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
            goto done;
        file_ofs += sizeof phdr;
        switch (phdr.p_type) {
            case PT_NULL:
            case PT_NOTE:
            case PT_PHDR:
            case PT_STACK:
            default:
                /* 이 세그먼트를 무시합니다. */
                /* Ignore this segment. */
                break;
            case PT_DYNAMIC:
            case PT_INTERP:
            case PT_SHLIB:
                goto done;
            case PT_LOAD:
                if (validate_segment(&phdr, file)) {
                    bool writable = (phdr.p_flags & PF_W) != 0;
                    uint64_t file_page = phdr.p_offset & ~PGMASK;
                    uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
                    uint64_t page_offset = phdr.p_vaddr & PGMASK;
                    uint32_t read_bytes, zero_bytes;
                    if (phdr.p_filesz > 0) {
                        /* 일반적인 세그먼트.
                         * 디스크에서 초기 부분을 읽고 나머지는 0으로 설정합니다. */
                        /* Normal segment.
                         * Read initial part from disk and zero the rest. */
                        read_bytes = page_offset + phdr.p_filesz;
                        zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) - read_bytes);
                    } else {
                        /* 완전히 0입니다.
                         * 디스크에서 아무 것도 읽지 않습니다. */
                        /* Entirely zero.
                         * Don't read anything from disk. */
                        read_bytes = 0;
                        zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
                    }
                    if (!load_segment(file, file_page, (void *)mem_page, read_bytes, zero_bytes, writable))
                        goto done;
                } else
                    goto done;
                break;
        }
    }

    /* 스택 설정 */
    /* Set up stack. */
    if (!setup_stack(if_))
        goto done;

    /* 시작 주소 */
    /* Start address. */
    if_->rip = ehdr.e_entry;

    /* TODO: 여기에 코드를 작성하세요.
     * TODO: 인자 전달을 구현하세요 (참조: project2/argument_passing.html). */
    /* TODO: Your code goes here.
     * TODO: Implement argument passing (see project2/argument_passing.html). */

    success = true;

done:
    /* 로드가 성공했든 실패했든 여기에 도착합니다. */
    /* We arrive here whether the load is successful or not. */
    file_close(file);
    return success;
}

/* PHDR가 FILE에 대한 유효한 로드 가능 세그먼트를 설명하는지 확인하고,
 * 그렇다면 true를 반환하고 그렇지 않으면 false를 반환합니다. */
/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool validate_segment(const struct Phdr *phdr, struct file *file) {
    /* p_offset과 p_vaddr는 동일한 페이지 오프셋을 가져야 합니다. */
    /* p_offset and p_vaddr must have the same page offset. */
    if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
        return false;

    /* p_offset은 FILE 내부에 있어야 합니다. */
    /* p_offset must point within FILE. */
    if (phdr->p_offset > (uint64_t)file_length(file))
        return false;

    /* p_memsz는 p_filesz보다 적어도 커야 합니다. */
    /* p_memsz must be at least as big as p_filesz. */
    if (phdr->p_memsz < phdr->p_filesz)
        return false;

    /* 세그먼트는 비어 있으면 안 됩니다. */
    /* The segment must not be empty. */
    if (phdr->p_memsz == 0)
        return false;

    /* 가상 메모리 영역은 사용자 주소 공간 범위 내에서 시작하고 끝나야 합니다. */
    /* The virtual memory region must both start and end within the
       user address space range. */
    if (!is_user_vaddr((void *)phdr->p_vaddr))
        return false;
    if (!is_user_vaddr((void *)(phdr->p_vaddr + phdr->p_memsz)))
        return false;

    /* 영역은 커널 가상 주소 공간을 가로지르면 안 됩니다. */
    /* The region cannot "wrap around" across the kernel virtual
       address space. */
    if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
        return false;

    /* 페이지 0을 매핑하지 않습니다.
     * 페이지 0을 매핑하는 것은 좋지 않은 아이디어일 뿐만 아니라,
     * 페이지 0을 통과하는 null 포인터를 시스템 호출에 전달하는 사용자 코드가
     * memcpy() 등에서 null 포인터 어설션으로 인해 커널을 패닉 상태로 만들 수 있습니다. */
    /* Disallow mapping page 0.
       Not only is it a bad idea to map page 0, but if we allowed
       it then user code that passed a null pointer to system calls
       could quite likely panic the kernel by way of null pointer
       assertions in memcpy(), etc. */
    if (phdr->p_vaddr < PGSIZE)
        return false;

    /* 괜찮습니다. */
    /* It's okay. */
    return true;
}

#ifndef VM
/* 이 블록의 코드는 프로젝트 2에서만 사용됩니다.
 * 전체 프로젝트 2에 대해 이 함수를 구현하려면
 * #ifndef 매크로 외부에 구현하십시오. */
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() 도우미 함수 */
/* load() helpers. */
static bool install_page(void *upage, void *kpage, bool writable);

/* 파일의 오프셋 OFS에서 시작하는 세그먼트를 주소 UPAGE에 로드합니다.
 * 총 READ_BYTES + ZERO_BYTES 바이트의 가상 메모리가 다음과 같이 초기화됩니다.
 *
 * - UPAGE에서 시작하는 READ_BYTES 바이트는 OFS에서 시작하는 FILE에서 읽어야 합니다.
 *
 * - UPAGE + READ_BYTES의 ZERO_BYTES 바이트는 0으로 초기화되어야 합니다.
 *
 * 이 함수에 의해 초기화된 페이지는 WRITABLE이 true인 경우 사용자 프로세스에 의해 쓰기 가능해야 하며,
 * 그렇지 않은 경우 읽기 전용이어야 합니다.
 *
 * 성공하면 true를 반환하고, 메모리 할당 오류 또는 디스크 읽기 오류가 발생하면 false를 반환합니다. */
/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage, uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
    ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
    ASSERT(pg_ofs(upage) == 0);
    ASSERT(ofs % PGSIZE == 0);

    file_seek(file, ofs);
    while (read_bytes > 0 || zero_bytes > 0) {
        /* 이 페이지를 채우는 방법을 계산합니다.
         * 우리는 FILE에서 PAGE_READ_BYTES 바이트를 읽을 것입니다.
         * 그리고 마지막 PAGE_ZERO_BYTES 바이트를 0으로 채울 것입니다. */
        /* Do calculate how to fill this page.
         * We will read PAGE_READ_BYTES bytes from FILE
         * and zero the final PAGE_ZERO_BYTES bytes. */
        size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;

        /* 메모리 페이지를 가져옵니다. */
        /* Get a page of memory. */
        uint8_t *kpage = palloc_get_page(PAL_USER);
        if (kpage == NULL)
            return false;

        /* 이 페이지를 로드합니다. */
        /* Load this page. */
        if (file_read(file, kpage, page_read_bytes) != (int)page_read_bytes) {
            palloc_free_page(kpage);
            return false;
        }
        memset(kpage + page_read_bytes, 0, page_zero_bytes);

        /* 프로세스의 주소 공간에 이 페이지를 추가합니다. */
        /* Add the page to the process's address space. */
        if (!install_page(upage, kpage, writable)) {
            printf("fail\n");
            palloc_free_page(kpage);
            return false;
        }

        /* Advance. */
        read_bytes -= page_read_bytes;
        zero_bytes -= page_zero_bytes;
        upage += PGSIZE;
    }
    return true;
}

/* 사용자 스택에 제로 페이지를 매핑하여 최소 스택을 생성합니다. */
/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool setup_stack(struct intr_frame *if_) {
    uint8_t *kpage;
    bool success = false;

    kpage = palloc_get_page(PAL_USER | PAL_ZERO);
    if (kpage != NULL) {
        success = install_page(((uint8_t *)USER_STACK) - PGSIZE, kpage, true);
        if (success)
            if_->rsp = USER_STACK;
        else
            palloc_free_page(kpage);
    }
    return success;
}

/* 사용자 가상 주소 UPAGE에서 커널 가상 주소 KPAGE로의 매핑을 페이지 테이블에 추가합니다.
 * WRITABLE이 true이면 사용자 프로세스가 페이지를 수정할 수 있으며, 그렇지 않으면 읽기 전용입니다.
 * UPAGE가 이미 매핑되어 있지 않아야 합니다.
 * KPAGE는 일반적으로 palloc_get_page()를 사용하여 사용자 풀에서 얻은 페이지여야 합니다.
 * UPAGE가 이미 매핑되어 있거나 메모리 할당에 실패한 경우 false를 반환하고,
 * 성공하면 true를 반환합니다. */
/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool install_page(void *upage, void *kpage, bool writable) {
    struct thread *t = thread_current();

    /* 해당 가상 주소에 이미 페이지가 있는지 확인한 후 페이지를 매핑합니다. */
    /* Verify that there's not already a page at that virtual
     * address, then map our page there. */
    return (pml4_get_page(t->pml4, upage) == NULL && pml4_set_page(t->pml4, upage, kpage, writable));
}
#else
/* 여기부터는 프로젝트 3 이후에 사용될 코드입니다.
 * 프로젝트 2만을 위해 함수를 구현하려면 위 블록에 구현하세요. */
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool lazy_load_segment(struct page *page, void *aux) {
    /* TODO: 파일에서 세그먼트를 로드합니다. */
    /* TODO: 이 함수는 주소 VA에서 처음 페이지 폴트가 발생할 때 호출됩니다. */
    /* TODO: 호출하는 동안 VA를 사용할 수 있습니다. */

    /* TODO: Load the segment from the file */
    /* TODO: This called when the first page fault occurs on address VA. */
    /* TODO: VA is available when calling this function. */
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage, uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
    ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
    ASSERT(pg_ofs(upage) == 0);
    ASSERT(ofs % PGSIZE == 0);

    while (read_bytes > 0 || zero_bytes > 0) {
        /* Do calculate how to fill this page.
         * We will read PAGE_READ_BYTES bytes from FILE
         * and zero the final PAGE_ZERO_BYTES bytes. */
        size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;

        /* TODO: Set up aux to pass information to the lazy_load_segment. */
        void *aux = NULL;
        if (!vm_alloc_page_with_initializer(VM_ANON, upage, writable, lazy_load_segment, aux))
            return false;

        /* Advance. */
        read_bytes -= page_read_bytes;
        zero_bytes -= page_zero_bytes;
        upage += PGSIZE;
    }
    return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool setup_stack(struct intr_frame *if_) {
    bool success = false;
    void *stack_bottom = (void *)(((uint8_t *)USER_STACK) - PGSIZE);

    /* TODO: Map the stack on stack_bottom and claim the page immediately.
     * TODO: If success, set the rsp accordingly.
     * TODO: You should mark the page is stack. */
    /* TODO: Your code goes here */

    return success;
}
#endif /* VM */
