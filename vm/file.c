/* file.c: Implementation of memory backed file object (mmaped object). */

#include "userprog/process.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

void do_munmap (void *addr);
void* do_mmap (void *addr, size_t length, int writable, struct file *file, off_t offset);
/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
    printf("file_backed_swap_in\n");
    if (page == NULL) {
        return false;
    }

    struct file_page *file_page UNUSED = &page->file;
    
    struct lazy_load_info* aux = (struct lazy_load_info*)page->uninit.aux;
    struct file *file = aux->file;
    off_t offset = aux->ofs;
    size_t page_read_bytes = aux->read_bytes;
    size_t page_zero_bytes = aux->zero_bytes;

    file_seek(file, offset);

    if (file_read(file, kva, page_read_bytes)!=(int)page_read_bytes) {
        return false;
    }

    memset(kva+page_read_bytes, 0, page_zero_bytes);

    return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
    if (page == NULL) {
        return false;
    }

	struct file_page *file_page UNUSED = &page->file;
    struct lazy_load_info* aux = (struct lazy_load_info*)page->uninit.aux;
    struct file *file = aux->file;

    if(pml4_is_dirty(thread_current()->pml4, page->va)) {
        file_write_at(file, page->va, aux->read_bytes, aux->ofs);
        pml4_set_dirty(thread_current()->pml4, page->va, false);
    }
    pml4_clear_page(thread_current()->pml4, page->va);
    return true;

}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct thread *curr = thread_current();
	struct lazy_load_info* info = (struct lazy_load_info*)page->uninit.aux;
    if (pml4_is_dirty(curr->pml4, page->va)){
		file_write_at(info->file, page->va, info->read_bytes, info->ofs);
		pml4_set_dirty(curr->pml4, page->va,0);
	} 
	pml4_clear_page(curr->pml4, page->va); 
}

void *
do_mmap(void *addr, size_t length, int writable,
        struct file *file, off_t offset)
{
    struct file *f = file_reopen(file);
    void *start_addr = addr; // 매핑 성공 시 파일이 매핑된 가상 주소 반환하는 데 사용

    size_t read_bytes = file_length(f) < length ? file_length(f) : length;
    size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;

    ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
    ASSERT(pg_ofs(addr) == 0);      // upage가 페이지 정렬되어 있는지 확인
    ASSERT(offset % PGSIZE == 0); // ofs가 페이지 정렬되어 있는지 확인

    while (read_bytes > 0 || zero_bytes > 0) {
        /* Do calculate how to fill this page.
         * We will read PAGE_READ_BYTES bytes from FILE
         * and zero the final PAGE_ZERO_BYTES bytes. */
        size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;

        /* TODO: Set up aux to pass information to the lazy_load_segment. */
        /*당신은 바이너리 파일을 로드할 때 필수적인 정보를 포함하는
        구조체를 생성하는 것이 좋습니다.*/
        struct lazy_load_info *container =
            (struct lazy_load_info *)malloc(sizeof(struct lazy_load_info));
        container->file = f;
        container->ofs = offset;
        container->read_bytes = page_read_bytes;
       	container->zero_bytes = page_zero_bytes;
        

        if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable,
                                            lazy_load_segment, container)) {
            return NULL;
        }
        /* Advance. */
        read_bytes -= page_read_bytes;
        zero_bytes -= page_zero_bytes;
        addr += PGSIZE;
        offset += page_read_bytes;
    }

    return start_addr;
}

void do_munmap (void *addr) {

	while(true){
		struct thread *curr = thread_current();
		struct page *find_page = spt_find_page(&curr->spt, addr);
		// struct frame *find_frame =find_page->frame;
		
		if (find_page == NULL) {
    		return NULL;
    	}

		
		struct lazy_load_info* container = (struct lazy_load_info*)find_page->uninit.aux;
		// 페이지의 dirty bit이 1이면 true를, 0이면 false를 리턴한다.
		if (pml4_is_dirty(curr->pml4, find_page->va)){
			// 물리 프레임에 변경된 데이터를 다시 디스크 파일에 업데이트 buffer에 있는 데이터를 size만큼, file의 file_ofs부터 써준다.
			file_write_at(container->file, addr, container->read_bytes, container->ofs);
			// dirty bit = 0
			// 인자로 받은 dirty의 값이 1이면 page의 dirty bit을 1로, 0이면 0으로 변경해준다.
			pml4_set_dirty(curr->pml4,find_page->va,0);
		} 
		
		pml4_clear_page(curr->pml4, find_page->va); 
		addr += PGSIZE;
	}
}