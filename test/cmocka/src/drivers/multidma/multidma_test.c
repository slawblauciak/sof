// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Slawomir Blauciak <slawomir.blauciak@linux.intel.com>

#include <ipc/channel_map.h>
#include <sof/drivers/multidma.h>
#include <sof/lib/dma.h>
#include <sof/common.h>
#include <sof/spinlock.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>
#include "mock_dma.h"

struct multidma_testcase {
	struct dma *dma;
	struct sof_ipc_stream_map *smap;
	void *buf;
	uint32_t period_bytes;
	uint32_t periods;
};

struct dma *dma_get(uint32_t dir, uint32_t cap, uint32_t dev, uint32_t flags)
{
	static struct dma mock_dma = {
		.ops = &mockdma_ops
	};

	return &mock_dma;
}

static struct multidma_testcase *testcases;

int dma_sg_alloc(struct dma_sg_elem_array *elem_array,
		 int zone,
		 uint32_t direction,
		 uint32_t buffer_count, uint32_t buffer_bytes,
		 uintptr_t dma_buffer_addr, uintptr_t external_addr)
{
	int i;

	elem_array->elems = calloc(buffer_count, sizeof(struct dma_sg_elem));

	assert_non_null(elem_array->elems);

	for (i = 0; i < buffer_count; i++) {
		elem_array->elems[i].size = buffer_bytes;

		switch (direction) {
		case DMA_DIR_MEM_TO_DEV:
		case DMA_DIR_LMEM_TO_HMEM:
			elem_array->elems[i].src = dma_buffer_addr;
			elem_array->elems[i].dest = external_addr;
			break;
		default:
			elem_array->elems[i].src = external_addr;
			elem_array->elems[i].dest = dma_buffer_addr;
			break;
		}

		dma_buffer_addr += buffer_bytes;
	}

	elem_array->count = buffer_count;

	return 0;
}

static void test_drivers_multidma_channel_get(void **state)
{
	//struct multidma_testcase *tc = *((struct multidma_testcase **)state);
	struct multidma_testcase *tc = testcases;
	struct dma_chan_data *channel;

	channel = dma_channel_get(tc->dma, 0);

	assert_non_null(channel);

	dma_channel_put(channel);
}

static void test_drivers_multidma_config(void **state)
{
	//struct multidma_testcase *tc = *((struct multidma_testcase **)state);
	struct multidma_testcase *tc = testcases;
	struct dma_sg_config config;
	struct dma_chan_data *channel;
	int i;

	channel = dma_channel_get(tc->dma, 0);

	assert_non_null(channel);

	config.src_width = 4;
	config.dest_width = 4;
	config.multi.dma_info.num_links = 2;
	config.multi.ch_bytes = sizeof(uint32_t);
	config.multi.dma_caps = 0; // irrelevant
	config.multi.dma_dev = 0; // irrelevant
	config.multi.stream_map = tc->smap;

	for (i = 0; i < CHANNEL_MAP_MAX_LINKS; ++i) {
		config.multi.dma_info.elems[i].fifo = 0;
		config.multi.dma_info.elems[i].link_id = -1;
	}

	/* TODO: */
	config.multi.dma_info.elems[0].link_id = 5;
	config.multi.dma_info.elems[1].link_id = 7;

	dma_sg_alloc(&config.elem_array, 0, DMA_DIR_MEM_TO_DEV, tc->periods,
		     tc->period_bytes, (uintptr_t)tc->buf, (uintptr_t)NULL);

	assert_int_equal(dma_set_config(channel, &config), 0);

	struct multidma_chan_data *chdata = dma_chan_get_data(channel);

	assert_int_equal(chdata->num_links, 2);
	assert_int_equal(chdata->links[0].link, 5);
	assert_int_equal(chdata->links[1].link, 7);
	assert_int_equal(chdata->links[0].num_txforms, 2);
	assert_int_equal(chdata->links[1].num_txforms, 2);
	assert_ptr_equal(chdata->links[0].buf, chdata->buf);
	assert_ptr_equal((char *)chdata->links[1].buf, (char *)chdata->buf +
			 (tc->period_bytes / 2));
	assert_int_equal(chdata->links[0].roffsets[0], 8);
	assert_int_equal(chdata->links[0].roffsets[1], 0);
	assert_int_equal(chdata->links[1].roffsets[0], 4);
	assert_int_equal(chdata->links[1].roffsets[1], 12);

	dma_channel_put(channel);
}

#include <stdio.h>

static void test_drivers_multidma_copy(void **state)
{
	//struct multidma_testcase *tc = *((struct multidma_testcase **)state);
	struct multidma_testcase *tc = testcases;
	struct dma_sg_config config;
	struct dma_chan_data *channel;
	int i;

	channel = dma_channel_get(tc->dma, 0);

	assert_non_null(channel);

	config.src_width = 4;
	config.dest_width = 4;
	config.multi.ch_bytes = 4;
	config.multi.dma_caps = 0; // irrelevant
	config.multi.dma_dev = 0; // irrelevant
	config.multi.stream_map = tc->smap;

	for (i = 0; i < CHANNEL_MAP_MAX_LINKS; ++i) {
		config.multi.dma_info.elems[i].fifo = 0;
		config.multi.dma_info.elems[i].link_id = -1;
	}

	/* TODO: */
	config.multi.dma_info.elems[0].link_id = 5;
	config.multi.dma_info.elems[1].link_id = 7;

	dma_sg_alloc(&config.elem_array, 0, DMA_DIR_MEM_TO_DEV, tc->periods,
		     tc->period_bytes, (uintptr_t)tc->buf, (uintptr_t)NULL);

	assert_int_equal(dma_set_config(channel, &config), 0);


	struct multidma_chan_data *chdata = dma_chan_get_data(channel);

	uint32_t *src = (uint32_t *)tc->buf;
	uint32_t *dst5 = (uint32_t *)chdata->links[0].buf;
	uint32_t *dst7 = (uint32_t *)chdata->links[1].buf;

	src[0] = 0x01010101;
	src[1] = 0x02020202;
	src[2] = 0x03030303;
	src[3] = 0x04040404;

	src[4] = 0x11111111;
	src[5] = 0x22222222;
	src[6] = 0x33333333;
	src[7] = 0x44444444;

	printf("src %p\r\n", src);

	assert_null(dma_copy(channel, sizeof(uint32_t) * 8, 0));

	for(i = 0; i < 4; ++i)
		printf("dst5 %d: 0x%X\r\n", i, dst5[i]);

	for(i = 0; i < 4; ++i)
		printf("dst7 %d: 0x%X\r\n", i, dst7[i]);

	assert_int_equal(src[0], dst5[1]);
	assert_int_equal(src[1], dst7[0]);
	assert_int_equal(src[2], dst5[0]);
	assert_int_equal(src[3], dst7[1]);

	assert_int_equal(src[4], dst5[1 + 2]);
	assert_int_equal(src[5], dst7[0 + 2]);
	assert_int_equal(src[6], dst5[0 + 2]);
	assert_int_equal(src[7], dst7[1 + 2]);

	src[0] = 0x05050505;
	src[1] = 0x06060606;
	src[2] = 0x07070707;
	src[3] = 0x08080808;

	src[4] = 0x55555555;
	src[5] = 0x66666666;
	src[6] = 0x77777777;
	src[7] = 0x88888888;

	assert_null(dma_copy(channel, sizeof(uint32_t) * 8, 0));

	for(i = 0; i < 4; ++i)
		printf("dst5 %d: 0x%X\r\n", i, dst5[i]);

	for(i = 0; i < 4; ++i)
		printf("dst7 %d: 0x%X\r\n", i, dst7[i]);

	assert_int_equal(src[0], dst5[1]);
	assert_int_equal(src[1], dst7[0]);
	assert_int_equal(src[2], dst5[0]);
	assert_int_equal(src[3], dst7[1]);

	assert_int_equal(src[4], dst5[1 + 2]);
	assert_int_equal(src[5], dst7[0 + 2]);
	assert_int_equal(src[6], dst5[0 + 2]);
	assert_int_equal(src[7], dst7[1 + 2]);

	dma_channel_put(channel);
}

static int test_setup(void **state)
{
	//struct multidma_testcase *tc = *((struct multidma_testcase **)state);
	struct multidma_testcase *tc = testcases;
	tc->dma = (struct dma *)calloc(1, sizeof(struct dma));

	assert_non_null(tc->dma);

	tc->dma->plat_data.dir = DMA_DIR_MEM_TO_DEV | DMA_DIR_DEV_TO_MEM;
	tc->dma->plat_data.channels = MULTIDMA_MAX_CHANS;
	tc->dma->ops = &multidma_ops;
	tc->dma->lock = (spinlock_t *)calloc(1, sizeof(spinlock_t));

	assert_int_equal(dma_probe(tc->dma), 0);

	tc->period_bytes = 8 * sizeof(uint32_t);
	tc->periods = 1;

	tc->buf = calloc(tc->periods, tc->period_bytes);

	return 0;
}

static int test_teardown(void **state)
{
	//struct multidma_testcase *tc = *((struct multidma_testcase **)state);
	struct multidma_testcase *tc = testcases;

	assert_int_equal(dma_remove(tc->dma), 0);

	free(tc->dma);
	tc->dma = NULL;

	free(tc->buf);
	tc->buf = NULL;

	return 0;
}

static struct multidma_testcase *get_testcases()
{
	static struct multidma_testcase tc[1];
	struct sof_ipc_stream_map *smap;
	struct sof_ipc_channel_map *chmap;
	uint32_t smap_size;

	smap_size = sizeof(*smap) + (sizeof(smap->ch_map[0]) * 4) +
		sizeof(smap->ch_map[0].ch_coeffs[0]) * 4;
	tc[0].smap = (struct sof_ipc_stream_map *)malloc(smap_size);
	smap = tc[0].smap;

	smap->num_ch_map = 4;

	chmap = get_channel_map(smap, 0);

	chmap->ch_index = 0;
	chmap->ext_id = 5;
	chmap->ch_mask = BIT(1);
	chmap->ch_coeffs[0] = 1;

	chmap = get_channel_map(smap, 1);

	chmap->ch_index = 2;
	chmap->ext_id = 5;
	chmap->ch_mask = BIT(0);
	chmap->ch_coeffs[0] = 1;

	chmap = get_channel_map(smap, 2);

	chmap->ch_index = 1;
	chmap->ext_id = 7;
	chmap->ch_mask = BIT(0);
	chmap->ch_coeffs[0] = 1;

	chmap = get_channel_map(smap, 3);

	chmap->ch_index = 3;
	chmap->ext_id = 7;
	chmap->ch_mask = BIT(1);
	chmap->ch_coeffs[0] = 1;

	return tc;
}

int main(void)
{
	struct CMUnitTest tests[] = {
		cmocka_unit_test(test_drivers_multidma_channel_get),
		cmocka_unit_test(test_drivers_multidma_config),
		cmocka_unit_test(test_drivers_multidma_copy)
	};
	int i;

	testcases = get_testcases();

	for (i = 0; i < ARRAY_SIZE(tests); ++i) {
		tests[i].setup_func = test_setup;
		tests[i].teardown_func = test_teardown;
		//tests[i].initial_state = &testcases;
	}

	cmocka_set_message_output(CM_OUTPUT_TAP);

	return cmocka_run_group_tests(tests, NULL, NULL);
}
