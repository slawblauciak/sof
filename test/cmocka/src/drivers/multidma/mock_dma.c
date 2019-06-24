// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Slawomir Blauciak <slawomir.blauciak@linux.intel.com>

#include <sof/audio/component.h>
#include <sof/lib/dma.h>
#include <stdint.h>
#include <stdlib.h>
#include "mock_dma.h"

static struct dma_chan_data *mockdma_channel_get(struct dma *dma,
						 unsigned int channel)
{
	struct dma_chan_data *chan =
		(struct dma_chan_data *)calloc(1, sizeof(struct dma_chan_data));

	chan->dma = dma;
	chan->index = channel;

	return chan;
}

static void mockdma_channel_put(struct dma_chan_data *channel)
{
	free(channel);
}

static int mockdma_start(struct dma_chan_data *channel)
{
	return 0;
}

static int mockdma_stop(struct dma_chan_data *channel)
{
	return 0;
}

static int mockdma_copy(struct dma_chan_data *channel, int bytes,
			uint32_t flags)
{
	return 0;
}

static int mockdma_pause(struct dma_chan_data *channel)
{
	return 0;
}

static int mockdma_release(struct dma_chan_data *channel)
{
	return 0;
}

static int mockdma_status(struct dma_chan_data *channel,
			  struct dma_chan_status *status, uint8_t direction)
{
	return 0;
}

static int mockdma_set_config(struct dma_chan_data *channel,
			      struct dma_sg_config *config)
{
	return 0;
}

static int mockdma_set_cb(struct dma_chan_data *channel, int cb_type,
			  void (*cb)(void *data, uint32_t type,
				     struct dma_cb_data *next),
			  void *cb_data)
{
	return 0;
}

static int mockdma_pm_context_restore(struct dma *dma)
{
	return 0;
}

static int mockdma_pm_context_store(struct dma *dma)
{
	return 0;
}

static int mockdma_probe(struct dma *dma)
{
	return 0;
}

static int mockdma_remove(struct dma *dma)
{
	return 0;
}

static int mockdma_data_size(struct dma_chan_data *channel, uint32_t *avail,
			     uint32_t *free)
{
	return 0;
}

static int mockdma_get_attribute(struct dma *dma, uint32_t type,
				 uint32_t *value)
{
	return 0;
}

const struct dma_ops mockdma_ops = {
	.channel_get		= mockdma_channel_get,
	.channel_put		= mockdma_channel_put,
	.start			= mockdma_start,
	.stop			= mockdma_stop,
	.copy			= mockdma_copy,
	.pause			= mockdma_pause,
	.release		= mockdma_release,
	.status			= mockdma_status,
	.set_config		= mockdma_set_config,
	.set_cb			= mockdma_set_cb,
	.pm_context_restore	= mockdma_pm_context_restore,
	.pm_context_store	= mockdma_pm_context_store,
	.probe			= mockdma_probe,
	.remove			= mockdma_remove,
	.get_data_size		= mockdma_data_size,
	.get_attribute		= mockdma_get_attribute,
};
