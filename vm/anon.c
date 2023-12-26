/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include <bitmap.h>
#include <string.h>

#include "devices/disk.h"
#include "threads/mmu.h"
#include "lib/string.h"
#include "vm/vm.h"
/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in(struct page *page, void *kva);
static bool anon_swap_out(struct page *page);
static void anon_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
    .swap_in = anon_swap_in,
    .swap_out = anon_swap_out,
    .destroy = anon_destroy,
    .type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void vm_anon_init(void) {
    /* TODO: Set up the swap_disk. */
    swap_disk = disk_get(1, 1);   // get swap disk, (1=채널번호,1=디스크번호)는 스왑디스크를 위한 공간.
    size_t swap_size = disk_size(swap_disk)/SECTOR_CNT; 
    swap_table = bitmap_create(swap_size);  //스왑테이블 생성 by 비트맵. -> 각 비트는 섹터가 사용중 여부를 알려줌.
}

/* Initialize the file mapping */
bool anon_initializer(struct page *page, enum vm_type type, void *kva) {
    /* Set up the handler */
    if (page == NULL || kva == NULL) return false;

    struct uninit_page *uninit_page = &page->uninit;
    memset(uninit_page, 0, sizeof(struct uninit_page));  //uninit page 0으로 초기화

    page->operations = &anon_ops; //page->operations를 anon을 위한 ops로 변경.

    struct anon_page *anon_page = &page->anon;
    anon_page->swap_sector = -1;  //디스크의 어떤 섹터에도 매핑되지 않은상태.
    return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool anon_swap_in(struct page *page, void *kva) {
    struct anon_page *anon_page = &page->anon;

    size_t swap_idx = anon_page->swap_sector;

    if (!bitmap_test(swap_table, swap_idx)) 
        return false;  //swap_idx의 비트맵이, 해당 섹터의 사용여부를 알려준다.
        //bitmap_test -> false시 해당 섹터에 데이터가 없음.

    for (int i = 0; i < SECTOR_CNT; i++) {
        disk_read(swap_disk, swap_idx * SECTOR_CNT + i, kva + DISK_SECTOR_SIZE * i);
        // 2번째인자-> 읽어올 섹터의 위치, 3번째인자-> 쓸 메모리의 주소.
        // 디스크 -> 메모리 방향으로의 이동.
    }
    bitmap_set(swap_table, swap_idx, false); //swap idx를 false로 바꿈 -> 디스크섹터에는 자리가 비게된다.

    return true;
}


/* Swap out the page by writing contents to the swap disk. */
static bool anon_swap_out(struct page *page) {
    struct anon_page *anon_page = &page->anon;

    size_t empty_slot = bitmap_scan(swap_table, 0, 1, false); //비어있는 섹터 찾기.

    if (empty_slot == BITMAP_ERROR) { //섹터 꽉찬상태
        return false;
    }

    for (int i=0; i < SECTOR_CNT; i++) {
        disk_write(swap_disk, empty_slot * SECTOR_CNT + i, page->va + DISK_SECTOR_SIZE * i);
    }  //2번째인자-> 쓸 섹터의 위치, 3번째인자-> 읽어올 메모리 주소.
       //메모리 -> 디스크로의 이동.

    bitmap_set(swap_table, empty_slot, true); //섹터 사용 여부  true로 전환.
    pml4_clear_page(thread_current()->pml4, page->va);

    anon_page->swap_sector = empty_slot;
    return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void anon_destroy(struct page *page) {
    
    struct anon_page *anon_page = &page->anon;
}
