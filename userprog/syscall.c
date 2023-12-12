#include <debug.h>
#include "userprog/process.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/init.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

struct file *get_file_from_fd_table (int fd);
int add_file_to_fd_table (struct file *file);

struct lock filesys_lock;

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	// lock_init(&filesys_lock);
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

void check_address(void *addr) {
	struct thread *t = thread_current();
	if (!is_user_vaddr(addr) || addr == NULL || pml4_get_page(t->pml4 , addr) == NULL) {
		exit(-1);
	}
}

int add_file_to_fd_table (struct file *file) {
	struct thread *t = thread_current();
	struct file **fdt = t->fd_table;
	int fd = t->fd_idx;

	while (fdt[fd] != NULL&& fdt[fd]) {
		fd++;
	}

	t->fd_idx = fd;
	fdt[fd] = file;
	return fd;
}

struct file *get_file_from_fd_table (int fd) {
	struct thread *t = thread_current();
	if (fd < 0 || fd >= 128) {
		return NULL;
	}
	return t->fd_table[fd];
}

int alloc_fd(struct file *file) {
	struct thread *t = thread_current();

	lock_acquire(&t->fd_lock);
	for (int i = 2; i < 128; i++) {
		if (t->fd_table[i] == 0) {
			t->fd_table[i] = file;
			lock_release(&t->fd_lock);
			return i;
		}
	}
	lock_release(&t->fd_lock);
	return -1;
}

void release_fd(int fd) {
	struct thread *t = thread_current();

	if (fd >= 2 && fd < 128) {
		t->fd_table[fd] = 0;
	}
}

void fd_table_close() {
	struct thread *t = thread_current();

	lock_acquire(&t->fd_lock);
	for (int i = 2; i < 128; i++) {
		if (t->fd_table[i]) {
			file_close(t->fd_table[i]);
			t->fd_table[i] = NULL;
		}
	}
	lock_release(&t->fd_lock);
}

void halt(void) {
	power_off();
}

void exit (int status) {
	printf("%s: exit(%d)\n", thread_name(), status);
	thread_current()->exit_status = status;
	thread_exit();
}

tid_t fork (const char *thread_name, struct intr_frame *f) {
	check_address(thread_name);
	tid_t tid = process_fork(thread_name, f);

	if (tid == TID_ERROR) {
		return TID_ERROR;
	}
	return tid; 
}

int exec (const char *file) {
	check_address(file);
	char *fn_copy = palloc_get_page(0);
	if (fn_copy == NULL) {
		exit(-1);
	}
	strlcpy(fn_copy, file, PGSIZE);
    if (process_exec(fn_copy) == -1) {
		exit(-1);
	}
}

int wait (tid_t tid) {
	if (tid < 0 ) {
		return -1;
	}

	return process_wait (tid);
}

bool create (const char *file, unsigned initial_size) {
	check_address(file);
	
	if (filesys_create(file, initial_size)) {
		return true;
	}
	else {
		return false;
	}
}

bool remove (const char *file) {
	check_address(file);
	if (filesys_remove(file)) {
		return true;
	}
	else {
		return false;
	}
}

int open (const char *file) {
	check_address(file);

	struct file *file_info = filesys_open(file);

	if (file_info == NULL) {
		return -1;
	}

	int fd = alloc_fd(file_info);

	if (fd == -1) {
		file_close(file_info);
		return -1;
	}
	
	return fd;
}

int filesize (int fd) {
	struct file *fileobj = get_file_from_fd_table(fd);
	if (fileobj == NULL) {
		return -1;
	}
	file_length(fileobj);
}

int read (int fd, void *buffer, unsigned length) {
	check_address(buffer);

	unsigned char *buf = buffer;
	int bytesRead;

	if (fd == 0) { 
		char key;
		for (int bytesRead = 0; bytesRead < length; bytesRead++) {
			key = input_getc();
			*buf++ = key;

			if (key == '\0') {
				break;
			}
		}
	} 
	else if (fd == 1) {
		return -1;
	} 
	else {
		struct file *f = get_file_from_fd_table(fd);
		if (f == NULL) {
			return -1; 
		}
		// lock_acquire(&filesys_lock);
		bytesRead = file_read(f, buffer, length);
		// lock_release(&filesys_lock);
	}

	return bytesRead;
}

int write (int fd, const void *buffer, unsigned length) {
	check_address(buffer);
	struct file *f = get_file_from_fd_table(fd);
	int bytesRead;

	if (fd == 0) {
		return -1;
	} 
	else if (fd == 1) {
		putbuf(buffer, length);
		return length;
	}
	else {
		if (f == NULL) {
			return 0;
		}
		// lock_acquire(&filesys_lock);
		int bytesRead = file_write(f, buffer, length);
		// lock_release(&filesys_lock);
		return bytesRead;
	}
}

void seek (int fd, unsigned position) {

	struct file *file = get_file_from_fd_table(fd);
	check_address(file);
	if (file == NULL) {
		return;
	}
	file_seek(file, position);
}

unsigned tell (int fd) {
	struct file *file = get_file_from_fd_table(fd);
	check_address(file);
	if (file == NULL) {
		return;
	}
	file_tell(fd);
}

void close (int fd) {
	struct file *file = get_file_from_fd_table(fd);
	check_address(file);
	if (file) {
		file_close(file);
		release_fd(fd);
	}
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	int syscall_num = f->R.rax;
	switch (syscall_num) {
		case SYS_HALT:
			halt();			// pintos를 종료시키는 시스템 콜
			break;
		case SYS_EXIT:
			exit(f->R.rdi);	// 현재 프로세스를 종료시키는 시스템 콜
			break;
		case SYS_FORK:
			f->R.rax = fork(f->R.rdi, f);
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
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			seek(f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:
			f->R.rax = tell(f->R.rdi);
			break;
		case SYS_CLOSE:
			close(f->R.rdi);
			break;
		default:
			exit(-1);
			thread_exit();
	}
}
