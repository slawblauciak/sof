/*
 * Copyright (c) 2016, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the Intel Corporation nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: Marek Lucki <marekx.lucki@linux.intel.com>
 */

#include <stdint.h>
#include <stddef.h>
#include <sof/lock.h>
#include <sof/list.h>
#include <sof/stream.h>
#include <sof/audio/component.h>

#include <sof/ipc.h>

/* tracing */
#define trace_keydetect_dummy(__e) \
	trace_event(TRACE_CLASS_KEYDETECT_DUMMY, __e)
#define trace_keydetect_dummy_error(__e) \
	trace_error(TRACE_CLASS_KEYDETECT_DUMMY, __e)
#define tracev_keydetect_dummy(__e) \
	tracev_event(TRACE_CLASS_KEYDETECT_DUMMY, __e)

/* mixer component private data */
struct keydetect_dummy_data {
	uint32_t period_bytes;
	void(*keydetect_dummy_func)(struct comp_dev *dev,
		struct comp_buffer *sink, struct comp_buffer *source);
};

static void keydetect_dummy_function(struct comp_dev *dev,
	struct comp_buffer *sink, struct comp_buffer *source)
{
}

static struct comp_dev *keydetect_dummy_new(struct sof_ipc_comp *comp)
{
	struct comp_dev *dev;
	struct sof_ipc_comp_keydetect_dummy *keydetect_dummy;
	struct sof_ipc_comp_keydetect_dummy *ipc_keydetect_dummy =
		(struct sof_ipc_comp_keydetect_dummy *)comp;
	struct keydetect_dummy_data *cd;

	trace_keydetect_dummy("keydetect_dummy_new()");

	dev = rzalloc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM,
		COMP_SIZE(struct sof_ipc_comp_keydetect_dummy));
	if (!dev)
		return NULL;

	keydetect_dummy = (struct sof_ipc_comp_keydetect_dummy *)&dev->comp;
	memcpy(keydetect_dummy, ipc_keydetect_dummy,
		sizeof(struct sof_ipc_comp_keydetect_dummy));

	cd = rzalloc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM, sizeof(*cd));
	if (!cd) {
		rfree(dev);
		return NULL;
	}

	comp_set_drvdata(dev, cd);
	dev->state = COMP_STATE_READY;

	return dev;
}

static void keydetect_dummy_free(struct comp_dev *dev)
{
	struct keydetect_dummy_data *cd = comp_get_drvdata(dev);

	trace_keydetect_dummy("keydetect_dummy_free()");

	rfree(cd);
	rfree(dev);
}

/* set component audio stream parameters */
static int keydetect_dummy_params(struct comp_dev *dev)
{
	//struct comp_data *cd = comp_get_drvdata(dev);

	trace_keydetect_dummy("keydetect_dummy_params()");

	/* rewrite params format for all downstream */
	//dev->params.frame_fmt = cd->sink_format;

	return 0;
}

/* used to pass standard and bespoke commands (with data) to component */
static int keydetect_dummy_cmd(struct comp_dev *dev, int cmd, void *data,
		   int max_data_size)
{
	/* keydetect_dummy will use buffer "connected" status */
	return 0;
}

/* copy and process stream data from source to sink buffers */
static int keydetect_dummy_copy(struct comp_dev *dev)
{

	return 0;
}

static int keydetect_dummy_reset(struct comp_dev *dev)
{
	trace_keydetect_dummy("volume_reset()");

	comp_set_state(dev, COMP_TRIGGER_RESET);
	return 0;
}

static int keydetect_dummy_prepare(struct comp_dev *dev)
{
	struct keydetect_dummy_data *cd = comp_get_drvdata(dev);
	int ret;

	trace_keydetect_dummy("keydetect_dummy_prepare()");

	/* does keydetect_dummy already have active source streams ? */
	if (dev->state != COMP_STATE_ACTIVE) {

		/* currently inactive so setup */
		cd->keydetect_dummy_func = keydetect_dummy_function;

		ret = comp_set_state(dev, COMP_TRIGGER_PREPARE);
		if (ret < 0)
			return ret;
	}

	return -1;
}

/**
 * \brief Executes cache operation on keydetect_dummy component.
 * \param[in,out] dev keydetect_dummy base component device.
 * \param[in] cmd Cache command.
 */
static void keydetect_dummy_cache(struct comp_dev *dev, int cmd)
{
	struct keydetect_dummy_data *cd;

	switch (cmd) {
	case CACHE_WRITEBACK_INV:
		trace_keydetect_dummy("keydetect_dummy_cache(), CACHE_WRITEBACK_INV");

		cd = comp_get_drvdata(dev);

		dcache_writeback_invalidate_region(cd, sizeof(*cd));
		dcache_writeback_invalidate_region(dev, sizeof(*dev));
		break;

	case CACHE_INVALIDATE:
		trace_keydetect_dummy("keydetect_dummy_cache(), CACHE_INVALIDATE");

		dcache_invalidate_region(dev, sizeof(*dev));

		cd = comp_get_drvdata(dev);
		dcache_invalidate_region(cd, sizeof(*cd));
		break;
	}
}

struct comp_driver comp_keydetect_dummy = {
	.type	= SOF_COMP_KEYDETECT_DUMMY,
	.ops	= {
		.new		= keydetect_dummy_new,
		.free		= keydetect_dummy_free,
		.params		= keydetect_dummy_params,
		.cmd		= keydetect_dummy_cmd,
		.copy		= keydetect_dummy_copy,
		.prepare	= keydetect_dummy_prepare,
		.reset		= keydetect_dummy_reset,
		.cache		= keydetect_dummy_cache,
	},
};

void sys_comp_keydetect_dummy_init(void)
{
	comp_register(&comp_keydetect_dummy);
}
