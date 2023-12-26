/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1,1);

	size_t swap_size = disk_size(swap_disk) / 8;
	swap_table = bitmap_create(swap_size);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	struct uninit_page* uninit_page = &page->uninit;
	memset(uninit_page, 0, sizeof(struct uninit_page));

	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	anon_page->swap_sector = -1;
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	printf("anon_swap_in\n");
	struct anon_page *anon_page = &page->anon;

	int find_slot = anon_page->swap_sector;

	if (bitmap_test(swap_table, find_slot) == false)
		return false;
	
	for (int i =0; i < 8; i++) {
		disk_read(swap_disk, find_slot * 8 + i, kva + 512 * i);
	}
	bitmap_set(swap_table, find_slot, false);

	return true;
}

#define BITMAP_ERROR SIZE_MAX
/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	printf("anon_swap_out\n");
	struct anon_page *anon_page = &page->anon;

	int empty_slot = bitmap_scan (swap_table, 0, 1, false);

	if (empty_slot == BITMAP_ERROR) {
		return false;
	}

	for (int i; i < 8; i++) {
		disk_write(swap_disk, empty_slot*8 + i, page->va + 512*i);
	}

	bitmap_set(swap_table, empty_slot, true);
	pml4_clear_page(thread_current()->pml4, page->va);

	anon_page->swap_sector = empty_slot;
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	printf("anon_destroy\n");
	struct anon_page *anon_page = &page->anon;
	if (anon_page != NULL) {
		file_close(anon_page);
		anon_page = NULL;
	}
}
