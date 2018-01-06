#include <arch.h>
#include <driver/vga.h>
#include <zjunix/bootmm.h>
#include <zjunix/utils.h>


struct bootmm bmm;

unsigned int firstusercode_start;
unsigned int firstusercode_len;

/* this global variable manage the whole physical memory
 * the size of the array is the number of physical frame
 * it actually is a bitmap
 */
unsigned char bootmmmap[MACHINE_MMSIZE >> PAGE_SHIFT];

// const value for ENUM of mem_type
char *mem_type_msg[] = {
        "Kernel code/data",
        "Mm Bitmap",
        "Vga Buffer",
        "Kernel page directory",
        "Kernel page table",
        "Dynamic",
        "Reserved"
        // consist with the mm_usage
};
/* FUNC@:set the content of struct bootmm_info
 * INPUT:
 *  @para info: the pointer represent the meminfo, needed to be set
 *  @para start: start physical frame number
 *  @para end: end physical frame number
 * RETURN:
 *
 */
void set_mminfo(struct bootmm_info *info, unsigned int start, unsigned int end, unsigned int type) {
    info->startFramePtr = start;
    info->endFramePtr = end;
    info->type = type;
}
enum insert_mminfo_state{
    //insert_mminfo failed, the info has been full
    _INSERT_FAILED,
    //insert non-related mm_segment, a single part
    _INSERT_SINGLE,
    //insert forward-connecting segment
    _INSERT_FOWARD_CONNECT,
    //insert back-connecting segment
    _INSERT_BACK_CONNECT,
    //insert bridge-connecting segment
    _INSERT_BRIDGE_CONNECT
};

/* FUNC@:insert a bootmm_info into bootmm_sys, more specific, into info[]
 * INPUT:
 *  @para memoryManagement: represent the bootmm system
 *  @para start: start physical frame number
 *  @para end: end physical frame number
 *  @para type: the inserted mminfo's type
 * RETURN:
 *  if the value == 0, then insert failed!
 *  return value list:
 *      consider the info a, b, c; b is the inserted info, a is the info before b, c is the info behind b
 *		_INSERT_FAILED
 *		_INSERT_SINGLE -> insert non-related mm_segment
 *		_INSERT_FOWARD_CONNECT -> insert forward-connecting segment a---b
 *		_INSERT_BACK_CONNECT -> insert following-connecting segment b---c
 *		_INSERT_BRIDGE_CONNECT -> insert bridge-connecting segment(remove_mminfo is called for deleting) a---b---c
*/
unsigned int insert_mminfo(struct bootmm *memoryManage, unsigned int start, unsigned int end, unsigned int type) {

    unsigned int index;

    for (index = 0; index < memoryManage->countInfos; index++)
    {
        // ignore the type-mismatching items to find one likely
        // only executing when the type is mathced
        if (!memoryManage->info[index].type != type)
        {
            // condition 1:
            // inserted memoryManagement is connecting to the forwarding one
            if (memoryManage->info[index].endFramePtr == start - 1)
            {
                if ((index + 1) < memoryManage->countInfos)
                {
                    // current info is still not the last in info
                    if (memoryManage->info[index + 1].type != type)
                    {   //foward-connecting
                        //change the old info's end frame number to the inserted info's end frame number
                        memoryManage->info[index].endFramePtr = end;
                        return _INSERT_FOWARD_CONNECT;
                    } else {
                        //condition 2:
                        //connecting to the forwarding and following one
                        if (memoryManage->info[index + 1].startFramePtr - 1 == end)
                        {
                            //bridge-connecting
                            memoryManage->info[index].endFramePtr = memoryManage->info[index + 1].endFramePtr;
                            //remove the next element(index + 1)
                            remove_mminfo(memoryManage, index + 1); 
                            return _INSERT_BRIDGE_CONNECT; //bridge-connecting
                        }
                    }
                } else
                {   // current info is the last element in info
                    // extend the last element to containing the inserted memoryManagement
                    memoryManage->info[index].endFramePtr = end;
                    return _INSERT_FOWARD_CONNECT;
                }
            }
            //condition 3:
            if (memoryManage->info[index].startFramePtr - 1 == end) {
                // inserted memoryManagement is connecting to the following one
                kernel_printf("type of %d : %x, type: %x", index, memoryManage->info[index].type, type);
                memoryManage->info[index].startFramePtr = start;
                return _INSERT_BACK_CONNECT;
            }
        }
    }

    if (memoryManage->countInfos >= MAX_INFO)
        // the info is full, cannot insert any element
        return _INSERT_FAILED;  
    // a new single element
    set_mminfo(memoryManage->info + memoryManage->countInfos, start, end, type);//memoryManagement->info[memoryManagement->countInfors]
    ++memoryManage->countInfos;
    return _INSERT_SINGLE;  // individual segment(non-connecting to any other)
}

/* FUNC@:get one sequential memory area to be split into two parts
 * (set the former one.end = splitAddress-1)
 * (set the latter one.start = splitAddress)
 * INPUT:
 *  @para memoryManagement: represent the bootmm system
 *  @para index: the split info's index 
 *  @para spiltAddress: the spilt address which must be in between info's startPfn and endPfn
 * RETURN:
 *  0: failed
 *  1: succeed
 */
unsigned int split_mminfo(struct bootmm *memoryManagement, unsigned int index, unsigned int splitAddress) {
    // all of these three variables is frame address
    unsigned int start, end;
    unsigned int tmp;

    start = memoryManagement->info[index].startFramePtr;
    end = memoryManagement->info[index].endFramePtr;
    // page align operation
    splitAddress &= PAGE_ALIGN;  //&= 1111...111000000000..00

    if ((splitAddress <= start) || (splitAddress >= end))
        //the spilt address which must be in between info's startPfn and endPfn
        return 0;  // split_start out of range

    if (memoryManagement->countInfos == MAX_INFO)
        //info is full, cannot allocate more element, thus cannot execute the spilt operation
        return 0;  
    
    for (tmp = memoryManagement->countInfos - 1; tmp >= index; --tmp) 
    {
        // copy and move
        memoryManagement->info[tmp + 1] = memoryManagement->info[tmp];
    }
    // first-part of spilt mmInfo
    memoryManagement->info[index].endFramePtr = splitAddress - 1;
    // later-part of spilt mmInfo
    memoryManagement->info[index + 1].startFramePtr = splitAddress;
    memoryManagement->countInfos++;
    return 1;
}
/* FUNC@:remove the mminfo in the info[]
 * just like the array's operation
 * INPUT:
 *  @para memoryManagement: represent the bootmm system
 *  @para index: the removed info's index 
 * RETURN:
 *
 */
void remove_mminfo(struct bootmm *memoryManagement, unsigned int index) 
{
    unsigned int i;
    if (index >= memoryManagement->countInfos)
        return;
    //copy and move
    if (index + 1 < memoryManagement->countInfos) {
        for (i = (index + 1); i < memoryManagement->countInfos; i++) {
            memoryManagement->info[i - 1] = memoryManagement->info[i];
        }
    }
    memoryManagement->countInfos--;
}
/* FUNC@: initialization
 * INPUT:
 * RETURN:
 */
void init_bootmm() {
    unsigned int index;
    // unsigned char *t_map;
    unsigned int end;//the end address of the kernel
    //16MB, kernel's size
    end = 16 * 1024 * 1024;
    kernel_memset(&bmm, 0, sizeof(bmm));
    //bmm is the global struct variable
    bmm.physicalMemory = get_phymm_size();//get from the arch.h
    // number of all the frame
    bmm.maxPhysicalFrameNumber = bmm.physicalMemory >> PAGE_SHIFT; 
    //intial the bitmap's start and end address
    bmm.mapStart = bootmmmap;  
    bmm.mapEnd = bootmmmap + sizeof(bootmmmap);

    bmm.countInfos = 0;
    // initial the bitmap's value, all---->0
    kernel_memset(bmm.mapStart, PAGE_FREE, sizeof(bootmmmap));
    insert_mminfo(&bmm, 0, (unsigned int)(end - 1), _MM_KERNEL);
    bmm.lastAllocated = (((unsigned int)(end) >> PAGE_SHIFT) - 1);//convert to the page address

    for (index = 0; index < end>>PAGE_SHIFT; index++) {
        bmm.mapStart[index] = PAGE_USED;// change bitmap
    }
}

/*
 * FUNC@: set value of page-bitmap-indicator
 * INPUT:
 *  @param startFrameNumber	: page frame start node
 *  @param count	: the number of pages to be set(-->1)
 *  @param value	: the value to be set(PAGE_FREE or PAGE_USED)
 */
void set_maps(unsigned int startFrameNumber, unsigned int count, unsigned char value) {
    // while (count) {
    //     bmm.s_map[s_pfn] = (unsigned char)value;
    //     --count;
    //     ++s_pfn;
    // }
    unsigned int i = count;
    for(i = count; i > 0; i--){
        //set bitmap's value(PAGE_USED or PAGE_FREE)
        bmm.mapStart[startFrameNumber++] = (unsigned char)value;
    }
}

/*
 * FUNC@: find sequential page_cnt number of pages to allocate
 * INPUT:
 *  @param pageCount : the number of pages requested
 *  @param startPhysicalFrameNumber : the allocating begin page frame node
 *  @param endPhysicalFrameNumber : the allocating end page frame node
 *  @param allignPhysicalFrameNumber: the align address which has cut the page address.
 * RETURN:
 *  return value  = 0 :: allocate failed, 
 *  else return index(page start), the specific address
 */
unsigned char *find_pages(unsigned int pageCount, unsigned int startPhysicalFrameNumber, unsigned int endPhysicalFrameNumber
                            , unsigned int allignPhysicalFrameNumber) {
    unsigned int index, tmp;//used for loop variable
    unsigned int count;
    // to ensure that this page has not been used, get the first frame from start-next frame 
    startPhysicalFrameNumber += (allignPhysicalFrameNumber - 1);
    startPhysicalFrameNumber &= ~(allignPhysicalFrameNumber - 1);

    for (index = startPhysicalFrameNumber; index < endPhysicalFrameNumber;) {
        // this condition ensures that the frame finded is free
        if (bmm.mapStart[index] == PAGE_USED) {
            ++index;
            continue;// to find the first free frame
        }

        count = pageCount;
        tmp = index;
        while (count) {
            if (tmp >= endPhysicalFrameNumber)
                return 0;
            // reaching end, but allocate request still cannot be satisfied

            if (bmm.mapStart[tmp] == PAGE_FREE) {
                tmp++;  // find next possible free page
                count--;
            }
            if (bmm.mapStart[tmp] == PAGE_USED) {
                //refresh to find another continuous free space
                break;
            }
        }
        if (count == 0) {  
            // count = 0 indicates that the specified page-sequence found
            bmm.lastAllocated = tmp - 1;// last allocated frame number
            // update the bitmap
            set_maps(index, pageCount, PAGE_USED);
            //return the actual address
            return (unsigned char *)(index << PAGE_SHIFT);
        } else {
            // not enough continuous free space
            // to find in the loop again
            index = tmp + allignPhysicalFrameNumber;  
        }
    }
    return 0;
}
/* FUNC@: allocate enough pages(mem) for the kernel, it will use the find_pages funcrtion
 * INPUT:
 *  @size: the allocated size
 *  @type: which type this space will be used for
 *  @align: the align address, which has not cut the page address
 * RETURN:
 *  return value: 0 represent not found, else return index(page start), specific address( << PAGE_SHIFT)
 */
unsigned char *bootmm_alloc_pages(unsigned int size, unsigned int type, unsigned int align) {
    unsigned int sizeInPage;
    unsigned int alignInPage;
    unsigned char *res;

    // to get the best size, which is bigger than the original size
    // to ensure the page align and the enough space
    size += ((1 << PAGE_SHIFT) - 1);
    size &= PAGE_ALIGN;
    // size represented by pages number
    sizeInPage = size >> PAGE_SHIFT;
    // alignInpage the same as size
    alignInPage = align >> PAGE_SHIFT;
    // in normal case, going forward is most likely to find suitable area
    // starting from the last_alloc_end address to the max_pfn(end_address)
    // find area lastAllocated+1 ~~~ maxPhysicalFrameNumber
    res = find_pages(sizeInPage, bmm.lastAllocated + 1, bmm.maxPhysicalFrameNumber, alignInPage);

    if (res) 
    {
        // if find the suitable pages
        if(!insert_mminfo(&bmm, (unsigned int)res, (unsigned int)res + size - 1, type))
            kernel_printf("insert in bootmm failed");
        return res;
    }

    // when system request a lot of operations in booting, then some free area
    // will appear in the front part
    // start from the start address(0)
    // find area 0 ~~~~ lastAllocated
    res = find_pages(sizeInPage, 0, bmm.lastAllocated, alignInPage);
    if (res)
    {
        // if ind the suitable pages
        if(!insert_mminfo(&bmm, (unsigned int)res, (unsigned int)res + size - 1, type))
            kernel_printf("insert in bootmm failed");
        return res;
    }
    return 0;  // not found, return NULL
}
/* FUNC@: get bootmap's info
 * INPUT:
 *  @memsage: the input message to tell to input
 * RETURN:
 *  return value: 0 represent not found, else return index(page start), specific address( << PAGE_SHIFT)
 */
void bootmap_info() {
    unsigned int index;
    kernel_printf("Bootmm Map :\n");
    for (index = 0; index < bmm.countInfos; ++index) {
        kernel_printf("\t%x-%x : %s\n", bmm.info[index].startFramePtr, bmm.info[index].endFramePtr, mem_type_msg[bmm.info[index].type]);
    }
}
/* FUNC@: free bootmm_page
 * INPUT:
 *  @start: the start address of the free_memory 
 *  @size: the size of the free_memory
 * RETURN:
 */
void bootmm_free_pages(unsigned int start, unsigned size)
{
    unsigned int index, count;

    // count is the page_size number relative to size
    size &= ~((1 << PAGE_SHIFT) - 1);
    count = size >> PAGE_SHIFT;
    if(!count)
        return;
    
    start &= ~((1 << PAGE_SHIFT) - 1);
    for(index = 0; index < bmm.countInfos; index++){
        // find the right index
        if(bmm.info[index].endFramePtr < start)
            continue;
        if(bmm.info[index].startFramePtr > start)
            continue;
        if(start + size - 1 > bmm.info[index].endFramePtr)
            continue;
        break;
    }
    if(index == bmm.countInfos){
        kernel_printf("bootmm_free_pages: not alloc space(%x:%x)\n", start, size);
        return;
    }
    // the same as before
    unsigned int startInPage;
    startInPage = start >> PAGE_SHIFT;
    // update the bitMap
    set_maps(startInPage, count, PAGE_FREE);

    if(bmm.info[index].startFramePtr == start){
        if(bmm.info[index].endFramePtr == (start + size - 1))
            // the total space in this area should be free
            remove_mminfo(&bmm, index);
        else
            // restore the old mminfo, which needn't be free
            set_mminfo(&(bmm.info[index]), start + size, bmm.info[index].endFramePtr, bmm.info[index].type);            
    }
    else{
        if(bmm.info[index].endFramePtr == (start + size -1))
            set_mminfo(&(bmm.info[index]), bmm.info[index].startFramePtr, start - 1, bmm.info[index].type);
        else{
            // if the free space is in the mminfo[index] area,
            // first spilt this area
            split_mminfo(&bmm, index, start);
            split_mminfo(&bmm, index + 1, start + size);
            // then the index+1 represent the middle area
            remove_mminfo(&bmm, index + 1);
        }
    }

}