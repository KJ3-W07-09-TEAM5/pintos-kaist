#include "stdbool.h"
#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

typedef int tid_t;

void syscall_init (void);
void check_address(void *addr);

/* file descriptor */
// int add_file_to_fd_table (struct file *file);
struct file *get_file_from_fd_table (int fd);
int alloc_fd(struct file *file);
void release_fd(int fd);
void fd_table_close();

void halt(void);
void exit (int status);
tid_t fork (const char *thread_name, struct intr_frame *f);
int exec (const char *file);
int wait (tid_t tid);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned length);
int write (int fd, const void *buffer, unsigned length);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);



#endif /* userprog/syscall.h */
