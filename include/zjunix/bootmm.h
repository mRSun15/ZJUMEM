#ifndef _ZJUNIX_BOOTMM_H
#define _ZJUNIX_BOOTMM_H

extern unsigned char __end[];

enum mm_usage {
    _MM_KERNEL, // used or reserved by the kernel, could only be accessed by kernel
    _MM_MMMAP,  //used as the memory bitmap
    _MM_VGABUFF,//used as VGA buffer, could only be accessed by the kernel
    _MM_PDTABLE,//used for paging mechanism, represent page directory table
    _MM_PTABLE, //used for paging mechanism, represent page table
    _MM_DYNAMIC,//for dynamic usage
    _MM_RESERVED,//used for reserve
    _MM_COUNT// just as placeholder, get the count of above mem types
};

// record every part of mm's information, describe this memory's information
struct bootmm_info {
    //start frame pointer of this memoru
    unsigned int startFramePtr;
    //end frame pointer of this memory
    unsigned int endFramePtr;
    // type of mem_usage, described as above
    unsigned int type;
};
// typedef struct bootmm_info * p_mminfo;

// 4K per page
#define PAGE_SHIFT 12
#define PAGE_FREE 0x00
#define PAGE_USED 0xff

#define PAGE_ALIGN (~((1 << PAGE_SHIFT) - 1))//0x111111..1100000000

#define MAX_INFO 10
//bootmem system, this struct control the whole bootmemory system
//control the allocation and free
//describe the information
struct bootmm {
    // the actual machine's physical memory
    unsigned int physicalMemory;
    // record the max frame number
    unsigned int maxPhysicalFrameNumber;

    //use a bitmap to manage the physical memory, 0----free  1------busy
    // map begin address
    unsigned char* mapStart;
    // map end address
    unsigned char* mapEnd;

    // the recently allocated address(frame number)
    unsigned int lastAllocated;
    // get number of infos stored in bootmm now < MAX_INFO
    unsigned int countInfos;
    // record the usage of different type
    struct bootmm_info info[MAX_INFO];
};
// typedef struct bootmm * p_bootmm;

extern unsigned int firstusercode_start;
extern unsigned int firstusercode_len;

extern struct bootmm bmm;

extern unsigned int get_phymm_size();
//set the content of struct bootmm_info
extern void set_mminfo(struct bootmm_info* info, unsigned int start, unsigned int end, unsigned int type);
//insert a bootmm_info into bootmm_sys, more specific, into info[]
extern unsigned int insert_mminfo(struct bootmm *memoryManage, unsigned int start, unsigned int end, unsigned int type); 
//get one sequential memory area to be split into two parts
extern unsigned int split_mminfo(struct bootmm *memoryManagement, unsigned int index, unsigned int splitAddress);
//remove the mminfo in the info[]
extern void remove_mminfo(struct bootmm* memeoryManagement, unsigned int index);

extern void init_bootmm();
//set value of page-bitmap-indicator
extern void set_maps(unsigned int startFrameNumber, unsigned int count, unsigned char value);
// find sequential page_cnt number of pages to allocate
extern unsigned char* find_pages(unsigned int pageCount, unsigned int startPhysicalFrameNumber, unsigned int endPhysicalFrameNumber
                            , unsigned int allignPhysicalFrameNumber);
//allocate enough pages(mem) for the kernel, it will use the find_pages funcrtion
extern unsigned char* bootmm_alloc_pages(unsigned int size, unsigned int type, unsigned int align);
//get bootmap's info
extern void bootmap_info();

#endif
