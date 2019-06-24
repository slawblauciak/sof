// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2019 Intel Corporation. All rights reserved.
//
// Author: Slawomir Blauciak <slawomir.blauciak@linux.intel.com>

#ifndef __SOF_DRIVERS_MULTIDMA_H__
#define __SOF_DRIVERS_MULTIDMA_H__

#include <ipc/channel_map.h>
#include <sof/lib/dma.h>

#define MULTIDMA_MAX_CHANS	4
#define MULTIDMA_MAX_LINKS	CHANNEL_MAP_MAX_LINKS
#define MULTIDMA_MAX_TXFORMS    8

#define MULTIDMA_BUF_ALIGN      4
#define MULTIDMA_CPY_ALIGN      4
#define MULTIDMA_PERIOD_COUNT	3

extern const struct dma_ops multidma_ops;

struct multidma_dma {
	struct dma *dma;
	struct list_item dma_list_item;
};

struct multidma_chan_link {
	struct multidma_chan_data *chdata;
	struct dma_chan_data *channel;
	int32_t link;
	uint32_t roffsets[MULTIDMA_MAX_TXFORMS];
	uint32_t num_txforms;
	void *buf;
	void *buf_w_ptr;
	struct dma_sg_elem_array elem_array;
};

struct multidma_chan_data {
	struct dma *dma;
	struct multidma_chan_link links[MULTIDMA_MAX_LINKS];
	uint32_t num_links;
	void *src;
	void *src_r_ptr;
	uint32_t src_period_bytes;
	uint32_t src_bytes;
	uint32_t ch_bytes;
	uint32_t cb_expected;
	uint32_t last_copy_bytes;
	uint32_t link_buf_bytes;
	int foobar;
	void *buf;
};

#endif /* __SOF_DRIVERS_MULTIDMA_H__ */
