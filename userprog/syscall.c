#include "userprog/syscall.h"

#include <stdio.h>
#include <syscall-nr.h>

#include "devices/input.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "intrinsic.h"
#include "lib/string.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/loader.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/gdt.h"
#include "userprog/process.h"
#include "vm/vm.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
struct file *get_file_from_fd_table(int fd);
struct lock file_lock;
void *mmap(void *addr, size_t length, int writable, int fd, off_t offset);
void munmap(void *addr);
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

void syscall_init(void) {
    write_msr(MSR_STAR,
              ((uint64_t)SEL_UCSEG - 0x10) << 48 | ((uint64_t)SEL_KCSEG) << 32);
    write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

    /* The interrupt service rountine should not serve any interrupts
     * until the syscall_entry swaps the userland stack to the kernel
     * mode stack. Therefore, we masked the FLAG_FL. */
    write_msr(MSR_SYSCALL_MASK,
              FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

    lock_init(&file_lock);
}

struct page *check_address(void *addr) {
    if (is_kernel_vaddr(addr) || addr == NULL) exit(-1);

    return spt_find_page(&thread_current()->spt, addr);
}

int add_file_to_fd_table(struct file *file) {
    struct thread *curr = thread_current();
    struct file **fdt = curr->fd_table;

    while (curr->fd_idx < FDCOUNT_LIMIT && fdt[curr->fd_idx]) {
        curr->fd_idx++;
    }

    if (curr->fd_idx == FDCOUNT_LIMIT) {
        return -1;
    }

    fdt[curr->fd_idx] = file;

    return curr->fd_idx;
}

void halt(void) { power_off(); }

void exit(int status) {
    thread_current()->exit_status = status;
    printf("%s: exit(%d)\n", thread_name(), thread_current()->exit_status);
    thread_exit();
}

tid_t fork(const char *thread_name, struct intr_frame *if_) {
    check_address(thread_name);
    return process_fork(thread_name, if_);
}

int exec(const char *file) {
    check_address(file);
    if (process_exec((void *)file) < 0) {
        exit(-1);
    }
}

int wait(tid_t tid) { return process_wait(tid); }

bool create(const char *file, unsigned initial_size) {
    check_address(file);

    lock_acquire(&file_lock);
    bool success = filesys_create(file, initial_size);
    lock_release(&file_lock);

    return success;
}

bool remove(const char *file) {
    check_address(file);
    return filesys_remove(file);
}

int open(const char *file) {
    check_address(file);
    lock_acquire(&file_lock);
    struct file *file_info = filesys_open(file);
    lock_release(&file_lock);
    if (file_info == NULL) {
        return -1;
    }
    int fd = add_file_to_fd_table(file_info);
    if (fd == -1) {
        file_close(file_info);
    }
    return fd;
}

int filesize(int fd) { return file_length(get_file_from_fd_table(fd)); }

int read(int fd, void *buffer, unsigned length) {
    validate_buffer(buffer, length, true);
    int bytesRead = 0;
    if (fd == 0) {
        for (int i = 0; i < length; i++) {
            char c = input_getc();
            ((char *)buffer)[i] = c;
            bytesRead++;

            if (c == '\n') break;
        }
    } else if (fd == 1) {
        return -1;
    } else {
        struct file *f = get_file_from_fd_table(fd);
        if (f == NULL) {
            return -1;
        }
        lock_acquire(&file_lock);
        bytesRead = file_read(f, buffer, length);
        lock_release(&file_lock);
    }
    return bytesRead;
}

struct file *get_file_from_fd_table(int fd) {
    struct thread *t = thread_current();
    if (fd < 0 || fd >= 128) {
        return NULL;
    }
    return t->fd_table[fd];
}

void validate_buffer(void *buffer, size_t size, bool to_write) {
    if (buffer == NULL) exit(-1);

    if (buffer <= USER_STACK && buffer >= thread_current()->user_rsp) return;

    void *start_addr = pg_round_down(buffer);
    void *end_addr = pg_round_down(buffer + size);

    ASSERT(start_addr <= end_addr);
    for (void *addr = end_addr; addr >= start_addr; addr -= PGSIZE) {
        // printf("addr: %p\n", addr);
        struct page *pg = check_address(addr);
        if (pg == NULL) {
            exit(-1);
        }

        if (pg->writable == false && to_write == true) {
            exit(-1);
        }
    }
}

int write(int fd, const void *buffer, unsigned length) {
    validate_buffer(buffer, length, false);
    int bytesRead = 0;

    if (fd == 0) {
        return -1;
    } else if (fd == 1) {
        putbuf(buffer, length);
        return length;
    } else {
        struct file *f = get_file_from_fd_table(fd);
        if (f == NULL) {
            return -1;
        }
        lock_acquire(&file_lock);
        bytesRead = file_write(f, buffer, length);
        lock_release(&file_lock);
    }
    return bytesRead;
}

void seek(int fd, unsigned position) {
    struct file *f = get_file_from_fd_table(fd);
    if (f == NULL) {
        return;
    }
    file_seek(f, position);
}

unsigned tell(int fd) {
    struct file *f = get_file_from_fd_table(fd);
    if (f == NULL) {
        return -1;
    }
    return file_tell(f);
}

void close(int fd) {
    struct thread *t = thread_current();
    struct file **fdt = t->fd_table;
    if (fd < 0 || fd >= 128) {
        return;
    }
    if (fdt[fd] == NULL) {
        return;
    }
    file_close(fdt[fd]);
    fdt[fd] = NULL;
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f) {
    thread_current()->user_rsp = f->rsp;

    switch (f->R.rax) {
        case SYS_HALT:
            halt();
            break;
        case SYS_EXIT:
            exit(f->R.rdi);
            break;
        case SYS_FORK:
            f->R.rax = fork(f->R.rdi, f);
            break;
        case SYS_EXEC:
            if (exec(f->R.rdi) < 0) {
                exit(-1);
            }
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
        case SYS_MMAP:
            f->R.rax = mmap(f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
            break;
        case SYS_MUNMAP:
            munmap(f->R.rdi);
            break;
        default:
            exit(-1);
    }
}
void munmap(void *addr) { do_munmap(addr); }

void *mmap(void *addr, size_t length, int writable, int fd, off_t offset) {
    struct thread *t = thread_current();
    // 파일의 시작점(offset)이 page-align되지 않았을 때
    if (offset % PGSIZE != 0) {
        return NULL;
    }
    // 가상 유저 page 시작 주소가 page-align되어있지 않을 때
    /* failure case 2: 해당 주소의 시작점이 page-align되어 있는지 & user
     * 영역인지 & 주소값이 null인지 & length가 0이하인지*/
    if (pg_round_down(addr) != addr || is_kernel_vaddr(addr) || addr == NULL ||
        (long long)length <= 0) {
        return NULL;
    }
    // 매핑하려는 페이지가 이미 존재하는 페이지와 겹칠 때(==SPT에 존재하는
    // 페이지일 때)

    if (spt_find_page(&t->spt, addr)) {
        return NULL;
    }

    // 콘솔 입출력과 연관된 파일 디스크립터 값(0: STDIN, 1:STDOUT)일 때
    if (fd == 0 || fd == 1) {
        exit(-1);
    }
    // 찾는 파일이 디스크에 없는경우

    struct file *f = get_file_from_fd_table(fd);
    if (f == NULL) {
        return NULL;
    }

    return do_mmap(addr, length, writable, f, offset);
}