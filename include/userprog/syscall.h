#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "filesys/file.h"
#include "stdbool.h"
#include "threads/interrupt.h"
typedef int tid_t;

struct page *check_address(void *addr);
int add_file_to_fd_table(struct file *file);
void halt(void);
void exit(int status);
tid_t fork(const char *thread_name, struct intr_frame *if_);
int exec(const char *file);
int wait(tid_t tid);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned length);
int write(int fd, const void *buffer, unsigned length);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);

void syscall_init(void);

#endif /* userprog/syscall.h */
