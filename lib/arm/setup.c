/*
 * Initialize machine setup information and I/O.
 *
 * After running setup() unit tests may query how many cpus they have
 * (nr_cpus), how much free memory it has, and at what physical
 * address that free memory starts (memregions[1].{addr,size}),
 * printf() and exit() will both work, and (argc, argv) are ready
 * to be passed to main().
 *
 * Copyright (C) 2014, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#include "libcflat.h"
#include "libfdt/libfdt.h"
#include "devicetree.h"
#include "asm/spinlock.h"
#include "asm/setup.h"

extern unsigned long stacktop;
extern void io_init(void);
extern void setup_args(const char *args);

u32 cpus[NR_CPUS] = { [0 ... NR_CPUS-1] = (~0UL) };
int nr_cpus;

static struct spinlock memregion_lock;
struct memregion memregions[NR_MEMREGIONS];
int nr_memregions;

static void cpu_set(int fdtnode __unused, u32 regval, void *info __unused)
{
	assert(nr_cpus < NR_CPUS);
	cpus[nr_cpus++] = regval;
}

static void cpu_init(void)
{
	nr_cpus = 0;
	assert(dt_for_each_cpu_node(cpu_set, NULL) == 0);
}

static void memregions_init(phys_addr_t freemem_start)
{
	/* we only expect one membank to be defined in the DT */
	struct dt_pbus_reg regs[1];
	phys_addr_t addr, size, mem_end;

	nr_memregions = dt_get_memory_params(regs, 1);

	assert(nr_memregions > 0);

	addr = regs[0].addr;
	size = regs[0].size;
	mem_end = addr + size;

	assert(!(addr & ~PHYS_MASK) && !((mem_end-1) & ~PHYS_MASK));

	memregions[0].addr = PAGE_ALIGN(addr); /* PHYS_OFFSET */

	freemem_start = PAGE_ALIGN(freemem_start);
	assert(freemem_start >= PHYS_OFFSET && freemem_start < mem_end);

	memregions[0].size = freemem_start - PHYS_OFFSET;
	memregions[1].addr = freemem_start;
	memregions[1].size = mem_end - freemem_start;
	memregions[1].free = true;
	nr_memregions = 2;

#ifdef __arm__
	/*
	 * make sure 32-bit unit tests don't have any surprises when
	 * running without virtual memory, by ensuring the initial
	 * memory region uses 32-bit addresses. Other memory regions
	 * may have > 32-bit addresses though, and the unit tests are
	 * free to do as they wish with that.
	 */
	assert(!(memregions[0].addr >> 32));
	assert(!((memregions[0].addr + memregions[0].size - 1) >> 32));
#endif
}

struct memregion *memregion_new(phys_addr_t size)
{
	phys_addr_t freemem_start, mem_end;
	struct memregion *mr;

	spin_lock(&memregion_lock);

	mr = &memregions[nr_memregions-1];

	if (!mr->free || mr->size < size) {
		printf("%s: requested=0x%llx, free=0x%llx.\n", size,
				mr->free ? mr->size : 0ULL);
		return NULL;
	}

	mem_end = mr->addr + mr->size;

	mr->size = size;
	mr->free = false;

	freemem_start = PAGE_ALIGN(mr->addr + size);

	if (freemem_start < mem_end && nr_memregions < NR_MEMREGIONS) {
		mr->size = freemem_start - mr->addr;
		memregions[nr_memregions].addr = freemem_start;
		memregions[nr_memregions].size = mem_end - freemem_start;
		memregions[nr_memregions].free = true;
		++nr_memregions;
	}

	spin_unlock(&memregion_lock);

	return mr;
}

void memregions_show(void)
{
	int i;
	for (i = 0; i < nr_memregions; ++i)
		printf("%016llx-%016llx [%s]\n",
			memregions[i].addr,
			memregions[i].addr + memregions[i].size - 1,
			memregions[i].free ? "FREE" : "USED");
}

void setup(unsigned long arg __unused, unsigned long id __unused,
	   const void *fdt)
{
	const char *bootargs;
	u32 fdt_size;

	/*
	 * Move the fdt to just above the stack. The free memory
	 * then starts just after the fdt.
	 */
	fdt_size = fdt_totalsize(fdt);
	assert(fdt_move(fdt, &stacktop, fdt_size) == 0);
	assert(dt_init(&stacktop) == 0);

	memregions_init((unsigned long)&stacktop + fdt_size);

	io_init();
	cpu_init();

	assert(dt_get_bootargs(&bootargs) == 0);
	setup_args(bootargs);
}
