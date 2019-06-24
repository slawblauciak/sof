// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2019 Intel Corporation. All rights reserved.
//
// Author: Slawomir Blauciak <slawomir.blauciak@linux.intel.com>

#include <sof/drivers/multidai.h>
#include <sof/lib/alloc.h>
#include <sof/lib/dai.h>
#include <sof/lib/dma.h>
#include <sof/lib/memory.h>
#include <sof/trace/trace.h>
#include <ipc/dai.h>
#include <ipc/topology.h>
#include <user/trace.h>

#define trace_multidai(__e, ...) \
	trace_event(TRACE_CLASS_MULTIDAI, __e, ##__VA_ARGS__)
#define trace_multidai_error(__e, ...)	\
	trace_error(TRACE_CLASS_MULTIDAI, __e, ##__VA_ARGS__)
#define tracev_multidai(__e, ...) \
	tracev_event(TRACE_CLASS_MULTIDAI, __e, ##__VA_ARGS__)

struct multidai_dai {
	struct dai *dai;
	uint32_t id;
};

struct multidai_data {
	uint32_t num_dais;
	struct multidai_dai dais[MULTIDAI_MAX_LINKS];
};

static int multidai_trigger(struct dai *dai, int cmd, int direction)
{
	struct multidai_data *data = dai_get_drvdata(dai);
	int i;

	trace_multidai("multidai_trigger() cmd %d", cmd);

	for (i = 0; i < data->num_dais; ++i)
		dai_trigger(data->dais[i].dai, cmd, direction);

	return 0;
}

static void multidai_make_child_dai(struct dai *dai, uint32_t type,
				    struct sof_ipc_dai_config *config,
				    uint32_t id)
{
	struct multidai_data *data = dai_get_drvdata(dai);
	struct multidai_dai *child_dai;
	int i;

	for (i = 0; i < data->num_dais; ++i)
		if (data->dais[i].id == id)
			return; /* child already created */

	trace_multidai("multidai_make_child_dai() type %u id %u", type, id);

	child_dai = &data->dais[data->num_dais++];
	child_dai->dai = dai_get(type, id, DAI_CREAT);
	child_dai->id = id;
}

static int multidai_set_config(struct dai *dai,
			       struct sof_ipc_dai_config *config)
{
	struct sof_ipc_channel_map *ch_map;
	int i;

	trace_multidai("multidai_set_config() config->format = 0x%4x",
		       config->format);

	for (i = 0; i < config->multi.stream_map.num_ch_map; ++i) {
		ch_map = get_channel_map(&config->multi.stream_map, i);

		if (ch_map->ext_id != 0xFFFFFFFF)
			multidai_make_child_dai(dai, config->multi.dai_type,
						config, ch_map->ext_id);
	}

	return 0;
}

static int multidai_context_store(struct dai *dai)
{
	struct multidai_data *data = dai_get_drvdata(dai);
	int i;

	trace_multidai("multidai_context_store()");

	for (i = 0; i < data->num_dais; ++i)
		dai_pm_context_store(data->dais[i].dai);

	return 0;
}

static int multidai_context_restore(struct dai *dai)
{
	struct multidai_data *data = dai_get_drvdata(dai);
	int i;

	trace_multidai("multidai_context_restore()");

	for (i = 0; i < data->num_dais; ++i)
		dai_pm_context_restore(data->dais[i].dai);

	return 0;
}

static int multidai_probe(struct dai *dai)
{
	struct multidai_data *data;

	trace_multidai("multidai_probe()");

	data = rzalloc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM, sizeof(*data));
	dai_set_drvdata(dai, data);

	spinlock_init(&dai->lock);

	return 0;
}

static int multidai_remove(struct dai *dai)
{
	struct multidai_data *data = dai_get_drvdata(dai);
	int i;

	trace_multidai("multidai_remove()");

	for (i = 0; i < data->num_dais; ++i)
		dai_put(data->dais[i].dai);

	rfree(data);
	dai_set_drvdata(dai, NULL);

	return 0;
}

static int multidai_get_handshake(struct dai *dai, int direction, int stream_id)
{
	return 0;
}

static int multidai_get_fifo(struct dai *dai, int direction, int stream_id)
{
	return 0;
}

static int multidai_get_dma_info(struct dai *dai, int direction,
				 struct dma_p_info *dma_info)
{
	struct multidai_data *data = dai_get_drvdata(dai);
	struct dma_p_info_elem *dma_elem;
	struct multidai_dai *child_dai;
	int i;

	dma_info->num_links = data->num_dais;

	for (i = 0; i < data->num_dais; ++i) {
		child_dai = &data->dais[i];

		dma_elem = &dma_info->elems[i];
		dma_elem->link_id = child_dai->id;
		dma_elem->fifo = dai_get_fifo(child_dai->dai, direction,
					      child_dai->id);
		dma_elem->handshake = dai_get_handshake(child_dai->dai,
							direction,
							child_dai->id);

		trace_multidai("multidai_get_dma_info(): "
			       "dai %d id %d fifo 0x%X handshake %d",
			       i, dma_elem->link_id, dma_elem->fifo,
			       dma_elem->handshake);
	}

	return 0;
}

const struct dai_driver multidai_driver = {
	.type = SOF_DAI_MULTIDAI,
	.dma_caps = DMA_CAP_MULTI,
	.dma_dev = DMA_DEV_MULTI,
	.ops = {
		.trigger		= multidai_trigger,
		.set_config		= multidai_set_config,
		.pm_context_store	= multidai_context_store,
		.pm_context_restore	= multidai_context_restore,
		.probe			= multidai_probe,
		.get_handshake		= multidai_get_handshake,
		.get_fifo		= multidai_get_fifo,
		.get_dma_info		= multidai_get_dma_info,
		.remove			= multidai_remove,
	},
};
