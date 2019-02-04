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
#define trace_keydetect_dummy(__e, ...) \
	trace_event(TRACE_CLASS_KEYDETECT_DUMMY, __e, ##__VA_ARGS__)
#define trace_keydetect_dummy_error(__e, ...) \
	trace_error(TRACE_CLASS_KEYDETECT_DUMMY, __e, ##__VA_ARGS__)
#define tracev_keydetect_dummy(__e, ...) \
	tracev_event(TRACE_CLASS_KEYDETECT_DUMMY, __e, ##__VA_ARGS__)

/* keydetect_dummy component private data */
struct keydetect_dummy_data {
	uint32_t source_period_bytes;		/**< source number of period bytes */
	uint32_t sink_period_bytes;		/**< sink number of period bytes */
	enum sof_ipc_frame source_format;	/**< source frame format */
	enum sof_ipc_frame sink_format;		/**< sink frame format */
	uint32_t period_bytes;
	void(*keydetect_dummy_func)(struct comp_dev *dev,
		struct comp_buffer *sink, struct comp_buffer *source);
};

static void keydetect_dummy_function(struct comp_dev *dev,
	struct comp_buffer *sink, struct comp_buffer *source)
{
}

/**
 * \brief Sets keydetect_dummy control command.
 * \param[in,out] dev keydetect_dummy base component device.
 * \param[in,out] cdata Control command data.
 * \return Error code.
 */
static int keydetect_dummy_ctrl_set_cmd(struct comp_dev *dev,
	struct sof_ipc_ctrl_data *cdata)
{
	trace_keydetect_dummy("keydetect_dummy_ctrl_set_cmd()");

	return 0;
}

/**
 * \brief Gets keydetect_dummy control command.
 * \param[in,out] dev keydetect_dummy component device.
 * \param[in,out] cdata Control command data.
 * \return Error code.
 */
static int keydetect_dummy_ctrl_get_cmd(struct comp_dev *dev,
	struct sof_ipc_ctrl_data *cdata, int size)
{
	trace_keydetect_dummy("keydetect_dummy_ctrl_get_cmd()");

	return 0;
}

static struct comp_dev *keydetect_dummy_new(struct sof_ipc_comp *comp)
{
	struct comp_dev *dev;
	struct sof_ipc_comp_keydetect_dummy *keydetect_dummy;
	struct sof_ipc_comp_keydetect_dummy *ipc_keydetect_dummy =
		(struct sof_ipc_comp_keydetect_dummy *)comp;
	struct keydetect_dummy_data *kdd;

	trace_keydetect_dummy("keydetect_dummy_new()");

	dev = rzalloc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM,
		COMP_SIZE(struct sof_ipc_comp_keydetect_dummy));
	if (!dev)
		return NULL;

	keydetect_dummy = (struct sof_ipc_comp_keydetect_dummy *)&dev->comp;
	memcpy(keydetect_dummy, ipc_keydetect_dummy,
		sizeof(struct sof_ipc_comp_keydetect_dummy));

	kdd = rzalloc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM, sizeof(*kdd));
	if (!kdd) {
		rfree(dev);
		return NULL;
	}

	comp_set_drvdata(dev, kdd);
	dev->state = COMP_STATE_READY;

	return dev;
}

static void keydetect_dummy_free(struct comp_dev *dev)
{
	struct keydetect_dummy_data *kdd = comp_get_drvdata(dev);

	trace_keydetect_dummy("keydetect_dummy_free()");

	rfree(kdd);
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
	struct sof_ipc_ctrl_data *cdata = data;

	trace_keydetect_dummy("keydetect_dummy_cmd()");

	switch (cmd) {
	case COMP_CMD_SET_VALUE:
		return keydetect_dummy_ctrl_set_cmd(dev, cdata);
	case COMP_CMD_GET_VALUE:
		return keydetect_dummy_ctrl_get_cmd(dev, cdata, max_data_size);
	default:
		return -EINVAL;
	}
}

/* used to pass standard and bespoke commands (with data) to component */
static int keydetect_dummy_trigger(struct comp_dev *dev, int cmd)
{
	trace_keydetect_dummy("keydetect_dummy_trigger()");

	return comp_set_state(dev, cmd);

//mlucki
#if 0
	int ret;

	trace_keydetect_dummy("keydetect_dummy_trigger()");

	ret = comp_set_state(dev, cmd);
	if (ret < 0)
		return ret;

	switch (cmd) {
	case COMP_TRIGGER_START:
	case COMP_TRIGGER_RELEASE:
		return 1; /* no need to go downstream */
	case COMP_TRIGGER_PAUSE:
	case COMP_TRIGGER_STOP:
		dev->state = COMP_STATE_ACTIVE;
		return 1; /* no need to go downstream */
	default:
		break;
	}

	return 0; /* send cmd downstream */
#endif
}

/* copy and process stream data from source to sink buffers */
static int keydetect_dummy_copy(struct comp_dev *dev)
{
	struct keydetect_dummy_data *kdd = comp_get_drvdata(dev);
	struct comp_buffer *sink;
	struct comp_buffer *source;

	tracev_keydetect_dummy("keydetect_dummy_copy()");

	/* keydetect_dummy components will only ever have 1 source and 1 sink buffer */
	source = list_first_item(&dev->bsource_list,
		struct comp_buffer, sink_list);
	sink = list_first_item(&dev->bsink_list,
		struct comp_buffer, source_list);

	/* make sure source component buffer has enough data available and that
	 * the sink component buffer has enough free bytes for copy. Also
	 * check for XRUNs
	 */
	if (source->avail < kdd->source_period_bytes) {
		trace_keydetect_dummy_error("keydetect_dummy_copy() error: "
			"source component buffer"
			" has not enough data available");
		comp_underrun(dev, source, kdd->source_period_bytes, 0);
		return -EIO;	/* xrun */
	}
	if (sink->free < kdd->sink_period_bytes) {
		trace_keydetect_dummy_error("keydetect_dummy_copy() error: "
			"sink component buffer"
			" has not enough free bytes for copy");
		comp_overrun(dev, sink, kdd->sink_period_bytes, 0);
		return -EIO;	/* xrun */
	}


	/* calc new free and available */
	comp_update_buffer_produce(sink, kdd->sink_period_bytes);
	comp_update_buffer_consume(source, kdd->source_period_bytes);

	return dev->frames;


#if 0
	struct keydetect_dummy_data *kdd = comp_get_drvdata(dev);


	trace_keydetect_dummy("keydetect_dummy_copy()");

	return 0;
#endif
}

static int keydetect_dummy_reset(struct comp_dev *dev)
{
	trace_keydetect_dummy("keydetect_dummy_reset()");

	comp_set_state(dev, COMP_TRIGGER_RESET);
	return 0;
}

static int keydetect_dummy_prepare(struct comp_dev *dev)
{
	struct keydetect_dummy_data *kdd = comp_get_drvdata(dev);
	struct comp_buffer *sinkb;
	struct comp_buffer *sourceb;
	struct sof_ipc_comp_config *config = COMP_GET_CONFIG(dev);
	int ret;


	trace_keydetect_dummy("keydetect_dummy_prepare()");

	ret = comp_set_state(dev, COMP_TRIGGER_PREPARE);
	if (ret < 0)
		return ret;

	kdd->keydetect_dummy_func = keydetect_dummy_function;

	/* keydetect_dummy components will only ever have 1 source and 1 sink buffer */
	sourceb = list_first_item(&dev->bsource_list,
		struct comp_buffer, sink_list);
	sinkb = list_first_item(&dev->bsink_list,
		struct comp_buffer, source_list);

	/* get source data format */
	comp_set_period_bytes(sourceb->source, dev->frames, &kdd->source_format,
		&kdd->source_period_bytes);

	/* get sink data format */
	comp_set_period_bytes(sinkb->sink, dev->frames, &kdd->sink_format,
		&kdd->sink_period_bytes);


	/* rewrite params format for all downstream */
	dev->params.frame_fmt = kdd->sink_format;

	dev->frame_bytes = kdd->sink_period_bytes / dev->frames;

	/* set downstream buffer size */
	ret = buffer_set_size(sinkb, kdd->sink_period_bytes *
		config->periods_sink);
	if (ret < 0) {
		trace_keydetect_dummy_error("volume_prepare() error: "
			"buffer_set_size() failed");
		goto err;
	}

	/* validate */
	if (kdd->sink_period_bytes == 0) {
		trace_keydetect_dummy_error("volume_prepare() error: "
			"kdd->sink_period_bytes = 0, dev->frames ="
			" %u, sinkb->sink->frame_bytes = %u",
			dev->frames, sinkb->sink->frame_bytes);
		ret = -EINVAL;
		goto err;
	}
	if (kdd->source_period_bytes == 0) {
		trace_keydetect_dummy_error("volume_prepare() error: "
			"kdd->source_period_bytes = 0, "
			"dev->frames = %u, "
			"sourceb->source->frame_bytes = %u",
			dev->frames, sourceb->source->frame_bytes);
		ret = -EINVAL;
		goto err;
	}

	return 0;

err:
	comp_set_state(dev, COMP_TRIGGER_RESET);
	return ret;


#if 0
	struct keydetect_dummy_data *kdd = comp_get_drvdata(dev);
	int ret;

	trace_keydetect_dummy("keydetect_dummy_prepare()");

	/* does keydetect_dummy already have active source streams ? */
	if (dev->state != COMP_STATE_ACTIVE) {

		/* currently inactive so setup */
		kdd->keydetect_dummy_func = keydetect_dummy_function;

		ret = comp_set_state(dev, COMP_TRIGGER_PREPARE);
		if (ret < 0)
			return ret;
	}

	return 0;
#endif
}

/**
 * \brief Executes cache operation on keydetect_dummy component.
 * \param[in,out] dev keydetect_dummy base component device.
 * \param[in] cmd Cache command.
 */
static void keydetect_dummy_cache(struct comp_dev *dev, int cmd)
{
	struct keydetect_dummy_data *kdd;

	switch (cmd) {
	case CACHE_WRITEBACK_INV:
		trace_keydetect_dummy("keydetect_dummy_cache(), CACHE_WRITEBACK_INV");

		kdd = comp_get_drvdata(dev);

		dcache_writeback_invalidate_region(kdd, sizeof(*kdd));
		dcache_writeback_invalidate_region(dev, sizeof(*dev));
		break;

	case CACHE_INVALIDATE:
		trace_keydetect_dummy("keydetect_dummy_cache(), CACHE_INVALIDATE");

		dcache_invalidate_region(dev, sizeof(*dev));

		kdd = comp_get_drvdata(dev);
		dcache_invalidate_region(kdd, sizeof(*kdd));
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
		.trigger	= keydetect_dummy_trigger,
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
