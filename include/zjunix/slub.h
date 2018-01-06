#ifndef _ZJUNIX_SLUB_H
#define _ZJUNIX_SLUB_H

#include <zjunix/list.h>
#include <zjunix/lock.h>

struct kmem_cache_node{
    lock_t lock;
    unsigned int nr_partial; // the number of partial slab in this node
    unsigned int nr_slabs; // the slab number in this node
    struct list_head partial; //double-way queue 
};

struct kmem_cache_cpu{
    void ** freelist; // free object's pointer, the first free object's
    struct page* page; //slab's first page_frame_pointer
    unsigned int node; //NUMA node number
    unsigned int offset; //store next free object's offset, (per word)
    unsigned int objsize; 
};

struct kmem_cache{
    long flags;  // describe the buffer's attribute 
    unsigned int size;  // memory size allocated to the object, may be larger than the objsize
    unsigned int objsize; //actual size of the object
    unsigned int offset;//  displacement of the free object pointer
    unsigned int order; // 2^order page frame
    kmem_cache_node local_node; // node which create buffer
    unsigned int object; //object number in a slab
    gfp_t allocflags;
    int refcount;
    unsigned int align;
    unsigned int inuse;
    unsigned char name[16];
    struct list_head list; //double-way queue
    unsigned int remote_node_defrag_ratio;
    struct kmem_cache_node node;
    struct kmem_cache_cpu cpu_slab;

};
#endif