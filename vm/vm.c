/* vm.c: Generic interface for virtual memory objects. */

#include "vm/vm.h"

#include <stdio.h>

#include "include/lib/kernel/hash.h"
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "vm/inspect.h"
struct list frame_table;
struct list_elem *clock_ref;
struct lock frame_table_lock;
/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void) {
    vm_anon_init();
    vm_file_init();
    list_init(&frame_table);
#ifdef EFILESYS /* For project 4 */
    pagecache_init();
#endif
    register_inspect_intr();
    lock_init(&frame_table_lock);
    lock_init(&kill_lock);

    /* DO NOT MODIFY UPPER LINES. */
    /* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type page_get_type(struct page *page) {
    int ty = VM_TYPE(page->operations->type);
    switch (ty) {
        case VM_UNINIT:
            return VM_TYPE(page->uninit.type);
        default:
            return ty;
    }
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage,
                                    bool writable, vm_initializer *init,
                                    void *aux) {
    ASSERT(VM_TYPE(type) != VM_UNINIT)
    struct supplemental_page_table *spt = &thread_current()->spt;

    /* Check wheter the upage is already occupied or not. */
    if (spt_find_page(spt, upage) == NULL) {
        /* TODO: Create the page, fetch the initialier according to the VM type,
         * TODO: and then create "uninit" page struct by calling uninit_new. You
         * TODO: should modify the field after calling the uninit_new. */
        struct page *new_page = (struct page *)malloc(sizeof(struct page));
        typedef bool (*page_initializer)(struct page *, enum vm_type,
                                         void *kva);
        page_initializer new_initializer = NULL;

        if (VM_TYPE(type) == VM_ANON) {
            new_initializer = anon_initializer;
            uninit_new(new_page, upage, init, VM_ANON, aux, new_initializer);
        }
        if (VM_TYPE(type) == VM_FILE) {
            new_initializer = file_backed_initializer;
            uninit_new(new_page, upage, init, VM_FILE, aux, new_initializer);
        }

        /* TODO: Insert the page into the spt. */
        new_page->writable = writable;
        return spt_insert_page(spt, new_page);
    }
err:
    return false;
}

/* Find VA from spt and return page. On error, return NULL. */
/*인자로 넘겨진 보조 페이지 테이블에서로부터
가상 주소(va)와 대응되는 페이지 구조체를 찾아서 반환합니다.
실패했을 경우 NULL를 반환합니다.*/
struct page *spt_find_page(struct supplemental_page_table *spt UNUSED,
                           void *va UNUSED) {
    struct page *page = NULL;
    /* TODO: Fill this function. */
    page = (struct page *)malloc(sizeof(struct page));
    struct hash_elem *h_e;

    page->va = pg_round_down(va);  // va를 페이지 경계로 내림하는 기능

    h_e = hash_find(&spt->hash_table, &page->hash_elem);

    free(page);

    if (h_e == NULL) {
        return NULL;
    } else {
        return hash_entry(h_e, struct page, hash_elem);
    }
}

/* Insert PAGE into spt with validation. */
/*인자로 주어진 보조 페이지 테이블에 페이지 구조체를 삽입합니다.
이 함수에서 주어진 보충 테이블에서 가상 주소가 존재하지 않는지 검사해야
합니다.*/
bool spt_insert_page(struct supplemental_page_table *spt UNUSED,
                     struct page *page UNUSED) {
    /* TODO: Fill this function. */
    if (hash_insert(&spt->hash_table, &page->hash_elem) == NULL) {
        return true;
    }
    return false;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page) {
    vm_dealloc_page(page);
    return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *vm_get_victim(void) {
    struct frame *victim = NULL;
    /* TODO: The policy for eviction is up to you. */
    struct thread *curr = thread_current();
    clock_ref = list_begin(&frame_table);
    
    lock_acquire(&frame_table_lock);
    for (clock_ref; clock_ref != list_end(&frame_table);
         clock_ref = list_next(clock_ref)) {
        victim = list_entry(clock_ref, struct frame, frame_elem);
        // bit가 1인 경우
        if (pml4_is_accessed(curr->pml4, victim->page->va)) {
            pml4_set_accessed(curr->pml4, victim->page->va, 0);
        } else {
            lock_release(&frame_table_lock);
            return victim;
        }
    }

    struct list_elem *iter = list_begin(&frame_table);

    for (iter; iter != list_end(&frame_table); iter = list_next(iter)) {
        victim = list_entry(iter, struct frame, frame_elem);
        // bit가 1인 경우
        if (pml4_is_accessed(curr->pml4, victim->page->va)) {
            pml4_set_accessed(curr->pml4, victim->page->va, 0);
        } else {
            lock_release(&frame_table_lock);
            return victim;
        }
    }
    
    lock_release(&frame_table_lock);
    ASSERT(clock_ref != NULL);
    return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *vm_evict_frame(void) {
    struct frame *victim UNUSED = vm_get_victim();
    /* TODO: swap out the victim and return the evicted frame. */
    //printf("victim is going well  kva = %p\n", victim->kva);
    swap_out(victim->page);
    return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *vm_get_frame(void) {
    struct frame *frame = NULL;
    frame = (struct frame *)malloc(sizeof(struct frame));

    /* TODO: Fill this function. */
    frame->page = NULL;
    frame->kva = palloc_get_page(PAL_USER);

    if (frame->kva == NULL) {
        frame = vm_evict_frame();
        frame->page = NULL;
        return frame;
    }
    ASSERT(frame != NULL);
    ASSERT(frame->page == NULL);

    lock_acquire(&frame_table_lock);
    list_push_back(&frame_table, &frame->frame_elem);
    lock_release(&frame_table_lock);

    //printf("frame_list size is %d\n", list_size(&frame_table));
    return frame;
}

/* Growing the stack. */
#define ONE_MB (1 << 20)
static void vm_stack_growth(void *addr UNUSED) {
    uintptr_t stack_bottom = pg_round_down(addr);
    vm_alloc_page(VM_ANON | VM_MARKER_0, stack_bottom, true);
}

/* Handle the fault on write_protected page */
static bool vm_handle_wp(struct page *page UNUSED) {}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED,
                         bool user UNUSED, bool write UNUSED,
                         bool not_present UNUSED) {
    struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
    struct page *page = NULL;
    /* TODO: Validate the fault */
    if (is_kernel_vaddr(addr) || addr == NULL || !not_present) {
        return false;
    }

    /* TODO: Your code goes here */
    uintptr_t stack_limit = USER_STACK - (1 << 20);
    uintptr_t rsp = user ? f->rsp : thread_current()->user_rsp;
    if (addr >= rsp - 8 && addr <= USER_STACK && addr >= stack_limit) {
        vm_stack_growth(addr);
    }

    if ((page = spt_find_page(spt, addr)) == NULL) {
        return false;
    }

    if (write && !(page->writable)) {
        return false;
    }

    if (!vm_do_claim_page(page)) {
        return false;
    }
    
    return true;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page) {
    destroy(page);
    free(page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page(void *va UNUSED) {
    struct page *page = NULL;
    /* TODO: Fill this function */
    page = spt_find_page(&thread_current()->spt, va);
    if (page == NULL) {
        return false;
    }

    return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool vm_do_claim_page(struct page *page) {
    struct frame *frame = vm_get_frame();

    /* Set links */
    frame->page = page;   // 여기서  frame에 page를 할당.
    page->frame = frame;  // 서로가 서로를 할당하는 모습

    bool set_page = false;
    /* TODO: Insert page table entry to map page's VA to frame's PA. */
    if (!pml4_get_page(thread_current()->pml4,
                       page->va)) {  // NULL이어야 기존것이 아님.
        set_page = pml4_set_page(thread_current()->pml4, page->va, frame->kva,
                                 page->writable);
        if (set_page) {
            return swap_in(page, frame->kva);
        }
    }

    return false;
}

/* Returns true if page a precedes page b. */
bool page_less(const struct hash_elem *a, const struct hash_elem *b,
               void *aux UNUSED) {
    const struct page *page_a = hash_entry(a, struct page, hash_elem);
    const struct page *page_b = hash_entry(b, struct page, hash_elem);
    return page_a->va < page_b->va;
}
/* Returns a hash value for page p. */
uint64_t page_hash(const struct hash_elem *h, void *aux UNUSED) {
    const struct page *p = hash_entry(h, struct page, hash_elem);
    return hash_bytes(&p->va, sizeof p->va);
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED) {
    hash_init(&spt->hash_table, page_hash, page_less, NULL);
}

bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
                                  struct supplemental_page_table *src UNUSED) {
    struct hash_iterator i;
    hash_first(&i, &src->hash_table);
    while (hash_next(&i)) {
        struct page *src_page =
            hash_entry(hash_cur(&i), struct page, hash_elem);
        /*vm_alloc_page_with_initializer에 필요한 인자들*/
        enum vm_type dst_type = page_get_type(src_page);     // 잠재타입
        enum vm_type now_type = src_page->operations->type;  // 현재타입
        void *dst_va = src_page->va;
        bool dst_writable = src_page->writable;

        /*else문에서 쓰임*/
        struct page *dst_page;

        /*UNINIT 처리*/
        if (now_type == VM_UNINIT) {
            vm_initializer *dst_init = src_page->uninit.init;
            void *dst_aux = src_page->uninit.aux;
            if (!vm_alloc_page_with_initializer(dst_type, dst_va, dst_writable,
                                                dst_init, dst_aux)) {
                return false;
            }
        }
        /*ANON, FILE 처리*/
        else {
            // 여기서는 now_type과 dst_type은 똑같겠지.
            if (!vm_alloc_page(now_type, dst_va, dst_writable)) {
                return false;
            }
            /*가짜 frame 할당받기*/
            if (!vm_claim_page(dst_va)) {
                return false;
            }
            dst_page = spt_find_page(dst, dst_va);

            memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
            /*memcpy PGSIZE인 이유는 kva가 palloc_get_page로부터 왔기 때문.*/
        }
    }

    return true;
}

void page_free(struct hash_elem *e, void *aux) {
    struct page *page_destroyed = hash_entry(e, struct page, hash_elem);
    free(page_destroyed);
}

void spt_destroy_func(struct hash_elem *e, void *aux) {
    struct page *pg = hash_entry(e, struct page, hash_elem);
    vm_dealloc_page(pg);
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED) {
    // /* TODO: Destroy all the supplemental_page_table hold by thread and
    hash_destroy(&spt->hash_table, spt_destroy_func);
    //  * TODO: writeback all the modified contents to the storage. */
}