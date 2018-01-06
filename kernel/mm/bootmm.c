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
char *mem_msg[] = {
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
 *  @para info: represent the meminfo, needed to be set
 *  @para start: start physical frame number
 *  @para end: end physical frame number
 * RETURN:
 *
 */
void set_mminfo(struct bootmm_info *info, unsigned int start, unsigned int end, unsigned int type) {
    info->start = start;
    info->end = end;
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

    for (index = 0; index < mm->cnt_infos; index++)
    {
        // ignore the type-mismatching items to find one likely
        // only executing when the type is mathced
        if (!memoryManage->info[index].type != type)
        {
            // condition 1:
            // inserted mm is connecting to the forwarding one
            if (memoryManage->info[index].endFramePtr == start - 1)
            {
                if ((index + 1) < memoryManage->cnt_infos)
                {
                    // current info is still not the last in info
                    if (memoryManage->info[index + 1].type != type)
                    {   //foward-connecting
                        //change the old info's end frame number to the inserted info's end frame number
                        memoryManage->info[index].end = end;
                        return _INSERT_FOWARD_CONNECT;
                    } else {
                        //condition 2:
                        if (memoryManage->info[index + 1].start - 1 == end)
                        {

                            memoryManage->info[index].end = mm->info[index + 1].end;
                            remove_mminfo(memoryManage, index + 1); //remove the next segment
                            return _INSERT_BRIDGE_CONNECT; //bridge-connecting
                        }
                    }
                } else
                {   // current info is the last element in info
                    // extend the last segment to containing the new-in mm
                    mm->info[index].end = end;
                    return _INSERT_FOWARD_CONNECT;
                }
            }
            //condition 3:
            if (mm->info[index].start - 1 == end) {
                // inserted mm is connecting to the following one
                kernel_printf("type of %d : %x, type: %x", index, mm->info[index].type, type);
                mm->info[index].start = start;
                return _INSERT_BACK_CONNECT;
            }
        }
    }

    if (mm->cnt_infos >= MAX_INFO)
        return _INSERT_FAILED;  // cannot
    set_mminfo(mm->info + mm->cnt_infos, start, end, type);//mm->info[mm->cnt_infos]
    ++mm->cnt_infos;
    return _INSERT_SINGLE;  // individual segment(non-connecting to any other)
}

/* get one sequential memory area to be split into two parts
 * (set the former one.end = split_start-1)
 * (set the latter one.start = split_start)
 */
unsigned int split_mminfo(struct bootmm *mm, unsigned int index, unsigned int split_start) {
    unsigned int start, end;
    unsigned int tmp;

    start = mm->info[index].start;
    end = mm->info[index].end;
    split_start &= PAGE_ALIGN;  //&= 1111...111000000000..00

    if ((split_start <= start) || (split_start >= end))
        return 0;  // split_start out of range

    if (mm->cnt_infos == MAX_INFO)
        return 0;  // number of segments are reaching max, cannot alloc anymore
                   // segments
    // using copy and move, to get a mirror segment of mm->info[index]
    for (tmp = mm->cnt_infos - 1; tmp >= index; --tmp) {
        mm->info[tmp + 1] = mm->info[tmp];
    }
    mm->info[index].end = split_start - 1;
    mm->info[index + 1].start = split_start;
    mm->cnt_infos++;
    return 1;
}

// remove the mm->info[index]
void remove_mminfo(struct bootmm *mm, unsigned int index) {
    unsigned int i;
    if (index >= mm->cnt_infos)
        return;

    if (index + 1 < mm->cnt_infos) {
        for (i = (index + 1); i < mm->cnt_infos; i++) {
            mm->info[i - 1] = mm->info[i];
        }
    }
    mm->cnt_infos--;
}

void init_bootmm() {
    unsigned int index;
    unsigned char *t_map;
    unsigned int end;
    end = 16 * 1024 * 1024;//16MB
    kernel_memset(&bmm, 0, sizeof(bmm));
    //bmm is the global struct variable
    bmm.phymm = get_phymm_size();
    bmm.max_pfn = bmm.phymm >> PAGE_SHIFT; 
    bmm.s_map = bootmmmap;  
    //page alianed??
    bmm.e_map = bootmmmap + sizeof(bootmmmap);
    //page alianed??
    bmm.cnt_infos = 0;
    kernel_memset(bmm.s_map, PAGE_FREE, sizeof(bootmmmap));
    insert_mminfo(&bmm, 0, (unsigned int)(end - 1), _MM_KERNEL);
    bmm.last_alloc_end = (((unsigned int)(end) >> PAGE_SHIFT) - 1);//convert to the page address

    for (index = 0; index< end>> PAGE_SHIFT; index++) {
        bmm.s_map[index] = PAGE_USED;
    }
}

/*
 * set value of page-bitmap-indicator
 * @param s_pfn	: page frame start node
 * @param cnt	: the number of pages to be set
 * @param value	: the value to be set
 */
void set_maps(unsigned int s_pfn, unsigned int cnt, unsigned char value) {
    while (cnt) {
        bmm.s_map[s_pfn] = (unsigned char)value;
        --cnt;
        ++s_pfn;
    }
}

/*
 * This function is to find sequential page_cnt number of pages to allocate
 * @param page_cnt : the number of pages requested
 * @param s_pfn    : the allocating begin page frame node
 * @param e_pfn	   : the allocating end page frame node
 * @param align_pfn: the align address which has cut the page address.
 * return value  = 0 :: allocate failed, else return index(page start), the specific address
 */
unsigned char *find_pages(unsigned int page_cnt, unsigned int s_pfn, unsigned int e_pfn, unsigned int align_pfn) {
    unsigned int index, tmp;
    unsigned int cnt;
    s_pfn += (align_pfn - 1);
    s_pfn &= ~(align_pfn - 1);

    for (index = s_pfn; index < e_pfn;) {
        if (bmm.s_map[index] == PAGE_USED) {
            ++index;
            continue;
        }

        cnt = page_cnt;
        tmp = index;
        while (cnt) {
            if (tmp >= e_pfn)
                return 0;
            // reaching end, but allocate request still cannot be satisfied

            if (bmm.s_map[tmp] == PAGE_FREE) {
                tmp++;  // find next possible free page
                cnt--;
            }
            if (bmm.s_map[tmp] == PAGE_USED) {
                break;
            }
        }
        if (cnt == 0) {  // cnt = 0 indicates that the specified page-sequence found
            bmm.last_alloc_end = tmp - 1;
            set_maps(index, page_cnt, PAGE_USED);
            return (unsigned char *)(index << PAGE_SHIFT);
        } else {
            index = tmp + align_pfn;  // there will be no possible memory space
                                      // to be allocated before tmp
        }
    }
    return 0;
}
/* This function is to allocate enough pages(mem) for the kernel, it will use the find_pages funcrtion
 * @size: the allocated size
 * @type: which type this space will be used for
 * @align: the align address, which has not cut the page address
 * return value: 0 represent not found, else return index(page start), specific address( << PAGE_SHIFT)
 */
unsigned char *bootmm_alloc_pages(unsigned int size, unsigned int type, unsigned int align) {
    unsigned int size_inpages;
    unsigned char *res;

    size += ((1 << PAGE_SHIFT) - 1);
    size &= PAGE_ALIGN;
    size_inpages = size >> PAGE_SHIFT;

    // in normal case, going forward is most likely to find suitable area
    // starting from the last_alloc_end address to the max_pfn(end_address)
    res = find_pages(size_inpages, bmm.last_alloc_end + 1, bmm.max_pfn, align >> PAGE_SHIFT);
    if (res) {
        insert_mminfo(&bmm, (unsigned int)res, (unsigned int)res + size - 1, type);
        return res;
    }

    // when system request a lot of operations in booting, then some free area
    // will appear in the front part
    // start from the start address(0)
    res = find_pages(size_inpages, 0, bmm.last_alloc_end, align >> PAGE_SHIFT);
    if (res) {
        insert_mminfo(&bmm, (unsigned int)res, (unsigned int)res + size - 1, type);
        return res;
    }
    return 0;  // not found, return NULL
}

void bootmap_info(unsigned char *msg) {
    unsigned int index;
    kernel_printf("%s :\n", msg);
    for (index = 0; index < bmm.cnt_infos; ++index) {
        kernel_printf("\t%x-%x : %s\n", bmm.info[index].start, bmm.info[index].end, mem_msg[bmm.info[index].type]);
    }
}
