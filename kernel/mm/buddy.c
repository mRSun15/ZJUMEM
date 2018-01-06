#include <driver/vga.h>
#include <zjunix/bootmm.h>
#include <zjunix/buddy.h>
#include <zjunix/list.h>
#include <zjunix/lock.h>
#include <zjunix/utils.h>

unsigned int kernel_start_pfn, kernel_end_pfn;

struct page *pages; // the global variable to reserve all the pages
//???where the pointer is reserved
struct buddy_sys buddy;

// void set_bplevel(struct page* bp, unsigned int bplevel)
//{
//	bp->bplevel = bplevel;
//}

void buddy_info() {
    unsigned int index;
    kernel_printf("Buddy-system :\n");
    kernel_printf("\tstart page-frame number : %x\n", buddy.buddy_start_pfn);
    kernel_printf("\tend page-frame number : %x\n", buddy.buddy_end_pfn);
    for (index = 0; index <= MAX_BUDDY_ORDER; ++index) {
        kernel_printf("\t(%x)# : %x frees\n", index, buddy.freelist[index].nr_free);
    }
}

// this function is to init all memory with page struct
void init_pages(unsigned int start_pfn, unsigned int end_pfn) {
    unsigned int i;
    for (i = start_pfn; i < end_pfn; i++) {
        clean_flag(pages + i, -1);
        set_flag(pages + i, _PAGE_RESERVED);
        (pages + i)->reference = 1;
        (pages + i)->virtual = (void *)(-1);
        (pages + i)->bplevel = (-1);
        (pages + i)->slabp = 0;  // initially, the free space is the whole page
        INIT_LIST_HEAD(&(pages[i].list));
    }
}

void init_buddy() {
    unsigned int bpsize = sizeof(struct page); //base_page's size
    unsigned char *bp_base;
    unsigned int i;

    // this function is to allocate enough spaces for Page_Frame_Space
    // all the memory is included, the kernel space is also allocated memory space
    bp_base = bootmm_alloc_pages(bpsize * bmm.maxPhysicalFrameNumber, _MM_KERNEL, 1 << PAGE_SHIFT);

    if (!bp_base) {
        // the remaining memory must be large enough to allocate the whole group
        // of buddy page struct
        kernel_printf("\nERROR : bootmm_alloc_pages failed!\nInit buddy system failed!\n");
        while (1)
            ;
    }

    // ?????, this code is used for?
    pages = (struct page *)((unsigned int)bp_base | 0x80000000);

    init_pages(0, bmm.maxPhysicalFrameNumber); //from pages[0] to pages[n]

    kernel_start_pfn = 0;
    kernel_end_pfn = 0;
    for (i = 0; i < bmm.countInfos; ++i) {
        if (bmm.info[i].endFramePtr > kernel_end_pfn)
            kernel_end_pfn = bmm.info[i].endFramePtr;
    }
    kernel_end_pfn >>= PAGE_SHIFT;

    //?????, why the MAX_BUDDY_ORDER
    buddy.buddy_start_pfn = (kernel_end_pfn + (1 << MAX_BUDDY_ORDER) - 1) &
                            ~((1 << MAX_BUDDY_ORDER) - 1);              // the pages that bootmm using cannot be merged into buddy_sys
    buddy.buddy_end_pfn = bmm.maxPhysicalFrameNumber & ~((1 << MAX_BUDDY_ORDER) - 1);  // remain 2 pages for I/O
    //both the start and end pfn is the page_frame_number

    // init freelists of all bplevels
    for (i = 0; i < MAX_BUDDY_ORDER + 1; i++) {
        buddy.freelist[i].nr_free = 0;
        INIT_LIST_HEAD(&(buddy.freelist[i].free_head));
    }
    buddy.start_page = pages + buddy.buddy_start_pfn;
    init_lock(&(buddy.lock));

    for (i = buddy.buddy_start_pfn; i < buddy.buddy_end_pfn; ++i) {
        __free_pages(pages + i, 0);
    }
}
/* This function is to free the pages
 * @pbpage: the first page's pointer which will be freed 
 * @bplevle: the order of the page block(represent size).
 */
void __free_pages(struct page *pbpage, unsigned int bplevel) {
    /* page_idx -> the current page
     * buddy_group_idx -> the buddy group that current page is in
     */
    unsigned int page_idx, buddy_group_idx;
    unsigned int combined_idx, tmp;
    struct page *buddy_group_page;

    // dec_ref(pbpage, 1);
    // if(pbpage->reference)
    //	return;

    lockup(&buddy.lock);

    page_idx = pbpage - buddy.start_page; 
    // complier do the sizeof(struct) operation, and now page_idx is the index

    while (bplevel < MAX_BUDDY_ORDER) {
        buddy_group_idx = page_idx ^ (1 << bplevel);
        buddy_group_page = pbpage + (buddy_group_idx - page_idx);
        // kernel_printf("group%x %x\n", (page_idx), buddy_group_idx);
        if (!_is_same_bplevel(buddy_group_page, bplevel)) {
            // kernel_printf("%x %x\n", buddy_group_page->bplevel, bplevel);

            break;
        }
        list_del_init(&buddy_group_page->list);
        --buddy.freelist[bplevel].nr_free;
        set_bplevel(buddy_group_page, -1);
        combined_idx = buddy_group_idx & page_idx;
        pbpage += (combined_idx - page_idx);
        page_idx = combined_idx;
        ++bplevel;
    }
    set_bplevel(pbpage, bplevel);
    list_add(&(pbpage->list), &(buddy.freelist[bplevel].free_head));
    ++buddy.freelist[bplevel].nr_free;
    // kernel_printf("v%x__addto__%x\n", &(pbpage->list),
    // &(buddy.freelist[bplevel].free_head));
    unlock(&buddy.lock);
}

struct page *__alloc_pages(unsigned int bplevel) {
    unsigned int current_order, size;
    struct page *page, *buddy_page;
    struct freelist *free;

    lockup(&buddy.lock);

    for (current_order = bplevel; current_order <= MAX_BUDDY_ORDER; ++current_order) {
        free = buddy.freelist + current_order;
        if (!list_empty(&(free->free_head)))
            goto found;
    }

    unlock(&buddy.lock);
    return 0;

found:
    page = container_of(free->free_head.next, struct page, list);
    list_del_init(&(page->list));
    set_bplevel(page, bplevel);
    set_flag(page, _PAGE_ALLOCED);
    // set_ref(page, 1);
    --(free->nr_free);

    size = 1 << current_order;
    while (current_order > bplevel) {
        --free;
        --current_order;
        size >>= 1;
        buddy_page = page + size;
        list_add(&(buddy_page->list), &(free->free_head));
        ++(free->nr_free);
        set_bplevel(buddy_page, current_order);
    }

    unlock(&buddy.lock);
    return page;
}

void *alloc_pages(unsigned int bplevel) {
    struct page *page = __alloc_pages(bplevel);

    if (!page)
        return 0;

    return (void *)((page - pages) << PAGE_SHIFT);
}

void free_pages(void *addr, unsigned int bplevel) {
    __free_pages(pages + ((unsigned int)addr >> PAGE_SHIFT), bplevel);
}
