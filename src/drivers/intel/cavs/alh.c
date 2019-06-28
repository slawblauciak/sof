// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2019 Intel Corporation. All rights reserved.
//
// Author: Slawomir Blauciak <slawomir.blauciak@linux.intel.com>

#include <errno.h>
#include <stdbool.h>
#include <sof/stream.h>
#include <sof/dai.h>
#include <sof/alloc.h>
#include <sof/interrupt.h>
#include <sof/pm_runtime.h>
#include <sof/math/numbers.h>
#include <config.h>

#define trace_alh(__e, ...) trace_event(TRACE_CLASS_ALH, __e, ##__VA_ARGS__)
#define trace_alh_error(__e, ...) \
	trace_error(TRACE_CLASS_ALH, __e, ##__VA_ARGS__)
#define tracev_alh(__e, ...) tracev_event(TRACE_CLASS_ALH, __e, ##__VA_ARGS__)

static const uint8_t alh_handshake_map[] = {
	-1,	/* 0  */
	-1,	/* 1  */
	-1,	/* 2  */
	-1,	/* 3  */
	-1,	/* 4  */
	-1,	/* 5  */
	-1,	/* 6  */
	22,	/* 7  */
	23,	/* 8  */
	24,	/* 9  */
	25,	/* 10 */
	26,	/* 11 */
	27,	/* 12 */
	-1,	/* 13 */
	-1,	/* 14 */
	-1,	/* 15 */
	-1,	/* 16 */
	-1,	/* 17 */
	-1,	/* 18 */
	-1,	/* 19 */
	-1,	/* 20 */
	-1,	/* 21 */
	-1,	/* 22 */
	32,	/* 23 */
	33,	/* 24 */
	34,	/* 25 */
	35,	/* 26 */
	36,	/* 27 */
	37,	/* 28 */
	-1,	/* 29 */
	-1,	/* 30 */
	-1,	/* 31 */
	-1,	/* 32 */
	-1,	/* 33 */
	-1,	/* 34 */
	-1,	/* 35 */
	-1,	/* 36 */
	-1,	/* 37 */
	-1,	/* 38 */
	42,	/* 39 */
	43,	/* 40 */
	44,	/* 41 */
	45,	/* 42 */
	46,	/* 43 */
	47,	/* 44 */
	-1,	/* 45 */
	-1,	/* 46 */
	-1,	/* 47 */
	-1,	/* 48 */
	-1,	/* 49 */
	-1,	/* 50 */
	-1,	/* 51 */
	-1,	/* 52 */
	-1,	/* 53 */
	-1,	/* 54 */
	52,	/* 55 */
	53,	/* 56 */
	54,	/* 57 */
	55,	/* 58 */
	56,	/* 59 */
	57,	/* 60 */
	-1,	/* 61 */
	-1,	/* 62 */
	-1,	/* 63 */
};

static int alh_trigger(struct dai *dai, int cmd, int direction)
{
	trace_alh("alh_trigger() cmd %d", cmd);

	return 0;
}

static int alh_set_config(struct dai *dai, struct sof_ipc_dai_config *config)
{
	trace_alh("alh_set_config() config->format = 0x%4x",
		  config->format);

	return 0;
}

static int alh_context_store(struct dai *dai)
{
	trace_alh("alh_context_store()");

	return 0;
}

static int alh_context_restore(struct dai *dai)
{
	trace_alh("alh_context_restore()");

	return 0;
}

static int alh_probe(struct dai *dai)
{
	trace_alh("alh_probe()");

	return 0;
}

static int alh_remove(struct dai *dai)
{
	trace_alh("alh_remove()");

	return 0;
}

static int alh_get_handshake(struct dai *dai, int direction, int stream_id)
{
	return alh_handshake_map[stream_id];
}

static int alh_get_fifo(struct dai *dai, int direction, int stream_id)
{
	uint32_t offset = direction == SOF_IPC_STREAM_PLAYBACK ?
		ALH_TXDA_OFFSET : ALH_RXDA_OFFSET;

	return ALH_BASE + offset + ALH_STREAM_OFFSET * stream_id;
}

const struct dai_driver alh_driver = {
	.type = SOF_DAI_INTEL_ALH,
	.dma_caps = DMA_CAP_GP_LP | DMA_CAP_GP_HP,
	.dma_dev = DMA_DEV_ALH,
	.ops = {
		.trigger		= alh_trigger,
		.set_config		= alh_set_config,
		.pm_context_store	= alh_context_store,
		.pm_context_restore	= alh_context_restore,
		.get_handshake		= alh_get_handshake,
		.get_fifo		= alh_get_fifo,
		.probe			= alh_probe,
		.remove			= alh_remove,
	},
};
