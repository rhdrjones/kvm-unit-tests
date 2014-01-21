#ifndef _ASMARM_SETUP_H_
#define _ASMARM_SETUP_H_
/*
 * Copyright (C) 2014, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#include "libcflat.h"

#define NR_CPUS			8
extern u32 cpus[NR_CPUS];
extern int nr_cpus;

typedef u64 phys_addr_t;

/*
 * memregions implement a very simple allocator which allows physical
 * memory to be partitioned into regions until all memory is allocated.
 * Also, as long as not all memory has been allocated, one region (the
 * highest indexable region) is used to represent the start and size of
 * the remaining free memory. This means that there will always be a
 * minimum of two regions: one for the unit test code, initially loaded
 * at the base of physical memory (PHYS_OFFSET), and another for the
 * remaining free memory.
 *
 * Note: This is such a simple allocator that there is no way to free
 * a memregion. For more complicated memory management a single region
 * can be allocated, but then have its memory managed by a more
 * sophisticated allocator, e.g. a page allocator.
 */
#define NR_MEMREGIONS		16
struct memregion {
	phys_addr_t addr;
	phys_addr_t size;
	bool free;
};

extern struct memregion memregions[NR_MEMREGIONS];
extern int nr_memregions;

/*
 * memregion_new returns a new memregion of size @size, or NULL if
 * there isn't enough free memory to satisfy the request.
 */
extern struct memregion *memregion_new(phys_addr_t size);

/*
 * memregions_show outputs all memregions with the following format
 *   <start_addr>-<end_addr> [<USED|FREE>]
 */
extern void memregions_show(void);

#define PHYS_OFFSET		({ memregions[0].addr; })
#define PHYS_SHIFT		40
#define PHYS_SIZE		(1ULL << PHYS_SHIFT)
#define PHYS_MASK		(PHYS_SIZE - 1ULL)

#define PAGE_SHIFT		12
#define PAGE_SIZE		(1UL << PAGE_SHIFT)
#define PAGE_MASK		(~(PAGE_SIZE - 1UL))
#define PAGE_ALIGN(addr)	(((addr) + (PAGE_SIZE-1UL)) & PAGE_MASK)

#endif /* _ASMARM_SETUP_H_ */
