#include "userprog/syscall.h"

#include <stdio.h>
#include <syscall-nr.h>
#include <user/syscall.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "intrinsic.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/loader.h"
#include "threads/thread.h"
#include "userprog/gdt.h"
#include "userprog/process.h"


void syscall_entry(void);
void syscall_handler(struct intr_frame *);
void check_address(void *addr);
struct file *fd_to_fileptr(int fd);
void halt();
void exit(int status);
bool create(const char *name, unsigned initial_size);
bool remove(const char *name);
int open(const char *name);
int write(int fd, const void *buffer, unsigned size);
int add_file_to_fdt(struct file *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
void seek (int fd, unsigned position);
pid_t fork (const char *thread_name);
unsigned tell(int fd);
void close (int fd);
int wait (pid_t pid);
int exec(const char *cmd_line);

static struct lock filesys_lock;
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

#define MSR_STAR 0xc0000081 /* 세그먼트 선택자 MSR */       /* Segment selector msr */
#define MSR_LSTAR 0xc0000082 /* Long mode SYSCALL target */ /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* eflags용 마스크 */   /* Mask for the eflags */

// check fd index
#define IS_UNVALID_FD(fd_index) (fd < fd_index || fd >= FDT_COUNT_LIMIT || t->fdt[fd] == NULL)


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

    lock_init(&filesys_lock);
}


/* 주요 시스템 호출 인터페이스 */
/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED) {
    // todo: 구현을 여기에 하세요
    // TODO: Your implementation goes here.

    int sys_num = f->R.rax;

    switch (sys_num) {
        case SYS_HALT:
            halt();
            break;
        case SYS_WRITE:
            f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
            break;
        case SYS_EXIT:
            exit(f->R.rdi);
            break;
        case SYS_FORK:
            memcpy(&thread_current()->parent_if, f, sizeof(struct intr_frame)); // 현재 thread의 parent_if에 if_를 저장
            f->R.rax = fork(f->R.rdi);
            break;
        case SYS_EXEC:
            f->R.rax = exec(f->R.rdi);
            break;
        case SYS_WAIT:
            f->R.rax = wait(f->R.rdi);
            break;
        case SYS_CREATE:
            f->R.rax = create(f->R.rdi, f->R.rsi);
            break;
        case SYS_REMOVE:
            f->R.rax = remove(f->R.rdi);
            break;
        case SYS_OPEN:
            f->R.rax = open(f->R.rdi);
            break;
        case SYS_FILESIZE:
            f->R.rax = filesize(f->R.rdi);
            break;
        case SYS_READ:
            f->R.rax = read(f->R.rdi,f->R.rsi,f->R.rdx);
            break;
        case SYS_SEEK:
            seek(f->R.rdi,f->R.rsi);
            break;
        case SYS_TELL:
            f->R.rax = tell(f->R.rdi);
            break;
        case SYS_CLOSE:
            close(f->R.rdi);
            break;
        default:
            thread_exit();
            break;
    }
}

/* 주소 유효성 검수하는 함수 */
void check_address(void *addr) {
    struct thread *t = thread_current();

    if (addr == NULL || !is_user_vaddr(addr))  // 사용자 영역 주소인지 확인
        exit(-1);
    if (pml4_get_page(t->pml4, addr) == NULL)  // 페이지로 할당된 영역인지 확인
        exit(-1);
}

/* fd로 file 주소를 반환하는 함수 */
struct file *fd_to_fileptr(int fd) {
  struct thread *t = thread_current();
  struct file *file = t->fdt[fd];

  // fd 값 검증
  if (fd < 0 || fd >= FDT_COUNT_LIMIT || file == NULL) {
    return NULL; // 유효하지 않은 파일 디스크립터
  }

  return file;
}

void halt() {
    power_off();
}

/* 현재 실행중인 스레드를 종료하는 함수 */
void exit(int status) {
    // 추후 종료 시 프로세스이름과 상태를 출력하는 메시지 추가
    struct thread *t = thread_current();
    t->exit_status = status;
    
    char *original_str = t->name; // 가정: t->name이 "dadsa-dasd o q"
    char *first_token,*save_ptr;

    first_token = strtok_r(original_str, " ",&save_ptr); // 공백을 구분자로 사용하여 첫 번째 토큰 추출

    if (first_token != NULL) {
        // 토큰이 성공적으로 추출되었다면, 이를 다루는 로직
          printf("%s: exit(%d)\n", first_token, t->exit_status);
    }
 
    thread_exit();
}

bool create(const char *name, unsigned initial_size) {
    check_address(name);
    return filesys_create(name, initial_size);
}

bool remove(const char *name) {
    check_address(name);
    return filesys_remove(name);
}

int open(const char *name) {
    check_address(name);
    struct file *file_obj = filesys_open(name);
    if (file_obj == NULL) {
        return -1;
    }

    int fd = add_file_to_fdt(file_obj);

    if (fd == -1) {
        file_close(file_obj);
    }

    return fd;
}

/* console 출력하는 함수 */
int write(int fd, const void *buffer, unsigned size) {
    check_address(buffer);
    if (fd == STDOUT_FILENO) {
        lock_acquire(&filesys_lock);
        putbuf(buffer, size);
        lock_release(&filesys_lock);
    }
    return size;
}

// ### fdt functions

// 파일을 현재스레드의 fdt에 추가
int add_file_to_fdt(struct file *file) {
    struct thread *t = thread_current();
    struct file **fdt = t->fdt;
    int fd = t->fd_idx;

    while (t->fdt[fd] != NULL && fd < FDT_COUNT_LIMIT) {
        fd++;
    }
    if (fd >= FDT_COUNT_LIMIT) {
        return -1;
    }

    t->fd_idx = fd;
    fdt[fd] = file;
    
    // filesize(fd);
    return fd;
}

void delete_file_from_fdt(int fd) {
    //fdt 에서 해당 fd값의 엔트리 null로 초기화
    struct thread *t = thread_current();
    t->fdt[fd] = NULL;
}

// fd (첫 번째 인자)로서 열려 있는 파일의 크기가 몇바이트인지 반환하는 함수
int filesize(int fd) {
    struct file *file = fd_to_fileptr(fd);

    // 유효하지 않은 fd
    if (file == NULL) {
      return - 1;
    }
    
    int size = file_length(file); 

    return size; 
}

// buffer 안에 fd로 열린 파일로 size 바이트를 읽음
int read(int fd, void *buffer, unsigned size) {
    struct file *file = fd_to_fileptr(fd);

    // 버퍼가 유효한 주소인지 체크
    check_address(buffer);

    // fd가 0이면 (stdin) input_getc()를 사용해서 키보드 입력을 받아옴
    if (fd == 0) {
        input_getc();
    }

    // 파일을 읽을 수 없는 케이스의 경우 -1 반환 , (fd값이 1인 경우 stout)
    if (file == NULL || fd == 1) {
        return -1; // 유효하지 않은 파일 디스크립터
    }

    // 구현 필요
    // lock을 이용해서 커널이 파일을 읽는 동안 다른 스레드가 이 파일을 읽는 것을 막아야함

    // filesys_lock 선언(syscall.h에 만들기)
    // syscall_init에도 lock 초기화함수 lock_init을 선언  
    lock_acquire(&filesys_lock);
    // 그 외는 파일 객체 찾고, size 바이트 크기 만큼 파일을 읽어서 버퍼에 넣어준다.
    off_t read_count = file_read (file, buffer, size);
    lock_release(&filesys_lock);

    return read_count;
}

// fd에서 읽거나 쓸 다음 바이트의 position을 변경해주는 함수
void seek (int fd, unsigned position) {
    struct file *file = fd_to_fileptr(fd);

    // 파일 디스크립터의 유효성을 검증
    if (file == NULL) {
        return -1;  // 유효하지 않은 파일 디스크립터로 인한 종료
    }

    file_seek (file, position);
}

unsigned tell (int fd) {
    struct file *file = fd_to_fileptr(fd);

    if (file == NULL) {
      return -1;
    }
    return file_tell(file);
}

void close (int fd) {
    struct file *file = fd_to_fileptr(fd);

    if (file == NULL) {
      return -1;
    }

    file_close(file);
    delete_file_from_fdt(fd);
}

pid_t fork (const char *thread_name) {
    check_address(thread_name);
    struct thread *t = thread_current();
    return process_fork(thread_name, &t->parent_if);
}

int wait (pid_t pid)
{
    /* 자식 프로세스가 종료 될 때까지 대기 */
    // 커널이 부모에게 자식의 종료 상태를 반환해줘야함
    // 자식의 종료 상태(exit status)를 가져옴
    // 만약 pid (자식 프로세스)가 아직 살아있으면, 종료 될 때 까지 기다립니다.
    //  종료가 되면 그 프로세스가 exit 함수로 전달해준 상태(exit status)를 반환합니다. 
	return process_wait(pid);
}

int exec (const char *cmd_line) {
    check_address(cmd_line);
    char *fn_copy;

    off_t size = strlen(cmd_line) + 1;
    fn_copy = palloc_get_page(0);
    if (fn_copy == NULL)
        return -1;
    strlcpy(fn_copy, cmd_line, size);  
    
    return process_exec(cmd_line);
}