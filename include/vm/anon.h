#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
#include "lib/kernel/bitmap.h"
#include "threads/vaddr.h"

#define SECTOR_CNT 8
#define DISK_SECTOR_SIZE 512
#define BITMAP_ERROR SIZE_MAX
struct page;
enum vm_type;

struct anon_page {
    int swap_sector; //섹터번호. -1이면, 메모리에있고 그외는 디스크(섹터)에 있음을 의미.
};
struct bitmap *swap_table;
int swap_size;
void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

#endif
