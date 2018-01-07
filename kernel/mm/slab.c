 #include <arch.h>
#include <driver/vga.h>
#include <zjunix/slab.h>
#include <zjunix/utils.h>

#define KMEM_ADDR(PAGE, BASE) ((((PAGE) - (BASE)) << PAGE_SHIFT) | 0x80000000)

/*
 * one list of PAGE_SHIFT(now it's 12) possbile memory size
 * 96, 192, 8, 16, 32, 64, 128, 256, 512, 1024, (2 undefined)
 * in current stage, set (2 undefined) to be (4, 2048)
 */
struct kmem_cache kmalloc_caches[PAGE_SHIFT];

static unsigned int size_kmem_cache[PAGE_SHIFT] = {96, 192, 8, 16, 32, 64, 128, 256, 512, 1024, 1536, 2048};

unsigned int judge_slab_free(struct kmem_cache *cache, void *object){
    //kernel_printf("judeg slab free condition, address is: %x", object);
    struct page* objectPage = pages + ((unsigned int)object >> PAGE_SHIFT);
    object = (void *)((unsigned int)object | KERNEL_ENTRY);
    void **freeSpacePtr = objectPage->slabp;
    unsigned int flag = 0;//not found
    if(objectPage->flag != _PAGE_SLAB)
        return 1;
    *freeSpacePtr = (void *)((unsigned int)(*freeSpacePtr) | KERNEL_ENTRY);
    while(!(is_bound((unsigned int )(*(freeSpacePtr)), 1 << PAGE_SHIFT)))
    {
        //kernel_printf("judge_slab_free: freeSpacePtr's value is: %x\n", *(freeSpacePtr));
        if(*(freeSpacePtr) == object)
        {    
            flag = 1;
            break;
        }
        freeSpacePtr = (void **)((unsigned int)*(freeSpacePtr) + cache->offset);
    }
    if(flag != 0)
        return 1;//represent in the free space, which has been freed.
    else
        return 0;
}
// init the struct kmem_cache_cpu
void init_kmem_cpu(struct kmem_cache_cpu *kcpu) {
    kcpu->page = 0;
    kcpu->freeobj = 0;
}

// init the struct kmem_cache_node
void init_kmem_node(struct kmem_cache_node *knode) {
    INIT_LIST_HEAD(&(knode->full));
    INIT_LIST_HEAD(&(knode->partial));
}

void init_each_slab(struct kmem_cache *cache, unsigned int size) {
    cache->objsize = size;
    cache->objsize += (SIZE_INT - 1);
    cache->objsize &= ~(SIZE_INT - 1);
    cache->size = cache->objsize + sizeof(void *);  // add one char as mark(available)
    cache->offset = size;
    init_kmem_cpu(&(cache->cpu));
    init_kmem_node(&(cache->node));
}

void init_slab() {
    unsigned int i;

    for (i = 0; i < PAGE_SHIFT; i++) {
        init_each_slab(&(kmalloc_caches[i]), size_kmem_cache[i]);
    }
#ifdef SLAB_DEBUG
    kernel_printf("Setup Slub ok :\n");
    kernel_printf("\tcurrent slab cache size list:\n\t");
    for (i = 0; i < PAGE_SHIFT; i++) {
        kernel_printf("%x %x ", kmalloc_caches[i].objsize, (unsigned int)(&(kmalloc_caches[i])));
    }
    kernel_printf("\n");
#endif  // ! SLAB_DEBUG
}

// ATTENTION: sl_objs is the reuse of bplevel
// ATTENTION: slabp must be set right to add itself to reach the end of the page
// 		e.g. if size = 96 then 4096 / 96 = .. .. 64 then slabp starts at
// 64
void format_slabpage(struct kmem_cache *cache, struct page *page) {
    unsigned char *moffset = (unsigned char *)KMEM_ADDR(page, pages);  // physical addr
    struct slab_head *s_head = (struct slab_head *)moffset;
    unsigned int *ptr;
    unsigned int remaining = (1 << PAGE_SHIFT);
    unsigned int startAddress;
    moffset += (unsigned int)sizeof(struct slab_head);
    //kernel_printf("slab_head's size is: %x\n", (unsigned int)sizeof(struct slab_head));

    remaining -=  sizeof(struct slab_head);
    startAddress = (unsigned int)moffset;
    set_flag(page, _PAGE_SLAB);
    do {
        ptr = (unsigned int *)(moffset + cache->offset);
        moffset += cache->size;
        *ptr = (unsigned int)moffset;
        remaining -= cache->size;
    } while (remaining >= cache->size);

    *ptr = (unsigned int)moffset & ~((1 << PAGE_SHIFT) - 1);
    s_head->end_ptr = ptr;
    s_head->nr_objs = 0;

    cache->cpu.page = page;
    cache->cpu.freeobj = (void **)(&startAddress);
    page->virtual = (void *)cache;
    page->slabp = (cache->cpu.freeobj);
}

void *slab_alloc(struct kmem_cache *cache) {
    struct slab_head *s_head;
    void *object = 0;
    struct page *newpage;

    if (cache->cpu.freeobj){
        object = *(cache->cpu.freeobj);
    }
    //kernel_printf("slab_alloc's object is: %x\n", object);
slalloc_check:
    // 1st: check if the freeobj is in the boundary situation
    if (is_bound((unsigned int)object, (1 << PAGE_SHIFT))) {
        // 2nd: the page is full
        if (cache->cpu.page) {
            list_add_tail(&(cache->cpu.page->list), &(cache->node.full));
        }

        if (list_empty(&(cache->node.partial))) {
            // call the buddy system to allocate one more page to be slab-cache
            newpage = __alloc_pages(0);  // get bplevel = 0 page === one page
            if (!newpage) {
                // allocate failed, memory in system is used up
                kernel_printf("ERROR: slab request one page in cache failed\n");
                while (1)
                    ;
            }
#ifdef SLAB_DEBUG
            kernel_printf("\tnew page, index: %x \n", newpage - pages);
#endif  // ! SLAB_DEBUG

        // using standard format to shape the new-allocated page,
        // set the new page to be cpu.page
            format_slabpage(cache, newpage);
            object = *(cache->cpu.freeobj);
            // as it's newly allocated no check may be need
            goto slalloc_check;
        }
        // get the header of the cpu.page(struct page)

        // This define is used to get the start pointer of a struct, if one of the member variable has been knowing, 
        // the input variable is:
        // ptr: the member variable's pointer;  type: the struct's type;   member: the member variable's name
        // the main computing eqution is "struct's address + member variable's relative address = ptr"
        // return the pointer of the struct
        //#define container_of(ptr, type, member) ((type*)((char*)ptr - (char*)&(((type*)0)->member)))
        cache->cpu.page = container_of(cache->node.partial.next, struct page, list);
        list_del(cache->node.partial.next);
        //kernel_printf("cpu.freeobj is: %x\n", cache->cpu.page->slabp);
        cache->cpu.freeobj = cache->cpu.page->slabp;
        object = (void *)(*(cache->cpu.freeobj));
        //kernel_printf("cpu.page->slabp is: %x\n", object);
        goto slalloc_check;
    }
slalloc_normal:
    cache->cpu.freeobj = (void **)((unsigned char *)object + cache->offset);
    cache->cpu.page->slabp = (cache->cpu.freeobj);
    s_head = (struct slab_head *)KMEM_ADDR(cache->cpu.page, pages);
    ++(s_head->nr_objs);
slalloc_end:
    // slab may be full after this allocation
    if (is_bound((unsigned int )(*(cache->cpu.page->slabp)), 1 << PAGE_SHIFT)) {
        kernel_printf("slab is full\n");
        list_add_tail(&(cache->cpu.page->list), &(cache->node.full));
        init_kmem_cpu(&(cache->cpu));
    }
    return object;
}

void slab_free(struct kmem_cache *cache, void *object) {
    if(judge_slab_free(cache, object)){
        kernel_printf("slab_free_failed, because it has been freed before\n");
        return;
    }
    struct page *opage = pages + ((unsigned int)object >> PAGE_SHIFT);
    object = (void *)((unsigned int)object | KERNEL_ENTRY);
    //kernel_printf("slab_free is : %x\n", object);
    unsigned int *ptr;
    struct slab_head *s_head = (struct slab_head *)KMEM_ADDR(opage, pages);
    //kernel_printf("slab_head is: %x\n", s_head);
    //kernel_printf("slab_free_1\n");
    //kernel_printf("slab_object is: %x\n", object);
    if (!(s_head->nr_objs)) {
        kernel_printf("ERROR : slab_free error!\n");
        // die();
        while (1)
            ;
    }

    ptr = (unsigned int *)((unsigned char *)object + cache->offset);
    //kernel_printf("ptr is: %x\n", ptr);
    //kernel_printf("end_ptr is: %x\n", s_head->end_ptr);
    if(ptr != s_head->end_ptr){
        *ptr = *((unsigned int *)(s_head->end_ptr));
        //kernel_printf("*ptr is: %x\n", *ptr);
        *((unsigned int *)(s_head->end_ptr)) = (unsigned int)object;
        s_head->end_ptr = ptr;
    }else{
        return;
    //     opage->slabp = (void**) &object;
    //     // kernel_printf("")
    //     kernel_printf("opage->slabp is: %x ", *(opage->slabp));
    //     if(cache->cpu.page == opage)
    //         cache->cpu.freeobj = opage->slabp;
    }
    --(s_head->nr_objs);
    //kernel_printf("slab_free_2\n");
    if (list_empty(&(opage->list))){
        return;
    }
    kernel_printf("slab_free successful, the address is %x\n", object);
        
    if (!(s_head->nr_objs)) {
        list_del_init(&(opage->list));
        //kernel_printf("free the page, since all of those page has been free\n");
        __free_pages(opage, 0);
        if(cache->cpu.page == opage){
            //kernel_printf("initialize the cache_cpu\n");
            init_kmem_cpu(&(cache->cpu));
        }
        //kernel_printf("slab free finished! \n");
        return;
    }
    //kernel_printf("slab_free_3\n");
    list_del_init(&(opage->list));
    list_add_tail(&(opage->list), &(cache->node.partial));
}

// find the best-fit slab system for (size)
unsigned int get_slab(unsigned int size) {
    unsigned int itop = PAGE_SHIFT;
    unsigned int i;
    unsigned int bf_num = (1 << (PAGE_SHIFT - 1));  // half page
    unsigned int bf_index = PAGE_SHIFT;             // record the best fit num & index

    for (i = 0; i < itop; i++) {
        if ((kmalloc_caches[i].objsize >= size) && (kmalloc_caches[i].objsize < bf_num)) {
            bf_num = kmalloc_caches[i].objsize;
            bf_index = i;
        }
    }
    return bf_index;
}

void *kmalloc(unsigned int size) {
    struct kmem_cache *cache;
    unsigned int bf_index;
    unsigned int bplevel;
    if (!size)
        return 0;

    // if the size larger than the max size of slab system, then call buddy to
    // solve this
    if (size > kmalloc_caches[PAGE_SHIFT - 1].objsize) {
        size += (1 << PAGE_SHIFT) - 1;
        size &= ~((1 << PAGE_SHIFT) - 1);    
        bplevel = 0;
        size = size >> (PAGE_SHIFT);
        size -=1;
        while(size > 0){
            size >>= 1;
            bplevel++;
        }
        //kernel_printf("bplevel is: %x\n", bplevel);
        return (void *)(KERNEL_ENTRY | (unsigned int)alloc_pages(bplevel));
    }

    bf_index = get_slab(size);
    if (bf_index >= PAGE_SHIFT) {
        kernel_printf("ERROR: No available slab\n");
        while (1)
            ;
    }
    return (void *)(KERNEL_ENTRY | (unsigned int)slab_alloc(&(kmalloc_caches[bf_index])));
}

void kfree(void *obj) {
    struct page *page;
    //kernel_printf("kfree\n");

    obj = (void *)((unsigned int)obj & (~KERNEL_ENTRY));
    page = pages + ((unsigned int)obj >> PAGE_SHIFT);
    if (!(page->flag == _PAGE_SLAB))
        return free_pages((void *)((unsigned int)obj & ~((1 << PAGE_SHIFT) - 1)), page->bplevel);
    //kernel_printf("slab_free\n");
    // return slab_free(page->virtual, (void *)((unsigned int)obj | KERNEL_ENTRY));
    return slab_free(page->virtual, obj);
       
}
