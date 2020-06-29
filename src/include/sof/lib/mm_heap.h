/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright(c) 2016 Intel Corporation. All rights reserved.
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 *         Keyon Jie <yang.jie@linux.intel.com>
 */

#ifndef __SOF_LIB_MM_HEAP_H__
#define __SOF_LIB_MM_HEAP_H__

#include <sof/common.h>
#include <sof/lib/alloc.h>
#include <sof/lib/cache.h>
#include <sof/lib/memory.h>
#include <sof/sof.h>
#include <sof/spinlock.h>

#include <stddef.h>
#include <stdint.h>

struct dma_copy;
struct dma_sg_config;

struct mm_info {
	uint32_t used;
	uint32_t free;
};

struct block_hdr {
	uint16_t size;		/* size in blocks for continuous allocation */
	uint16_t used;		/* usage flags for page */
	void *unaligned_ptr;	/* align ptr */
} __packed;

struct block_map {
	uint16_t block_size;	/* size of block in bytes */
	uint16_t count;		/* number of blocks in map */
	uint16_t free_count;	/* number of free blocks */
	uint16_t first_free;	/* index of first free block */
	struct block_hdr *block;	/* base block header */
	uint32_t base;		/* base address of space */
};

#define BLOCK_DEF(sz, cnt, hdr) \
	{.block_size = sz, .count = cnt, .free_count = cnt, .block = hdr, \
	 .first_free = 0}

struct mm_heap {
	uint32_t blocks;
	struct block_map *map;
	uint32_t heap;
	uint32_t size;
	uint32_t caps;
	struct mm_info info;
};

/* heap block memory map */
struct mm {
	/* system heap - used during init cannot be freed */
	struct mm_heap system[PLATFORM_HEAP_SYSTEM];
	/* system runtime heap - used for runtime system components */
	struct mm_heap system_runtime[PLATFORM_HEAP_SYSTEM_RUNTIME];
	/* general heap for components */
	struct mm_heap runtime[PLATFORM_HEAP_RUNTIME];
	/* general component buffer heap */
	struct mm_heap buffer[PLATFORM_HEAP_BUFFER];

	struct mm_info total;
	uint32_t heap_trace_updated;	/* updates that can be presented */
	spinlock_t lock;	/* all allocs and frees are atomic */
};

/* Heap save/restore contents and context for PM D0/D3 events */
uint32_t mm_pm_context_size(void);
int mm_pm_context_save(struct dma_copy *dc, struct dma_sg_config *sg);
int mm_pm_context_restore(struct dma_copy *dc, struct dma_sg_config *sg);

/* heap initialisation */
void init_heap(struct sof *sof);

/* frees entire heap (supported for slave core system heap atm) */
void free_heap(enum mem_zone zone, int core);

/* status */
void heap_trace_all(int force);
void heap_trace(struct mm_heap *heap, int size);

/* retrieve memory map pointer */
static inline struct mm *memmap_get(void)
{
	return sof_get()->memory_map;
}

#endif /* __SOF_LIB_MM_HEAP_H__ */
