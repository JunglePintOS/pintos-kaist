#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>

typedef int pid_t;

void syscall_init (void);
void halt();
void exit(int status);
bool create(const char *name, unsigned initial_size);
bool remove(const char *name);
int open(const char *name);
int write(int fd, const void *buffer, unsigned size);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
void seek (int fd, unsigned position);
pid_t fork (const char *thread_name);
unsigned tell(int fd);
void close (int fd);
int wait (pid_t pid);
int exec(const char *cmd_line);


#endif /* userprog/syscall.h */
