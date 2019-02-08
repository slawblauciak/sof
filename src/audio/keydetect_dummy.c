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


#define SOF_KEYDETECT_DUMMY_MAX_SIZE 1024

/* keydetect_dummy_configuration
 *	uint32_t fulfill_buf_size;
 *         This describes the size of empty fulfill buffer that
 *		   increases component size for tests
 *	uint32_t mips;
 *         This describes the number of additional stress operations
 *		   performed during stream processing
 */
struct sof_keydetect_dummy_config {
	uint32_t size;
	uint32_t fulfill_buf_size;	/**< size of dummy memory fulfillment */
	uint32_t mips;	/**< number of stream stress operations */
} __attribute__((packed));

/* keydetect_dummy component private data */
struct comp_data {
	uint32_t source_period_bytes; /**< source number of period bytes */
	uint32_t sink_period_bytes;	  /**< sink number of period bytes */
	enum sof_ipc_frame source_format;	/**< source frame format */
	enum sof_ipc_frame sink_format;		/**< sink frame format */
	uint32_t period_bytes;
	uint32_t tmp_level;

	char *fulfill_buff;			/**< dummy memory buffer */
	struct sof_keydetect_dummy_config *config;

	void (*main_dummy_func)(struct comp_dev *dev,
		struct comp_buffer *sink, struct comp_buffer *source);
};

static void main_dummy_function(struct comp_dev *dev,
	struct comp_buffer *sink, struct comp_buffer *source)
{
}

/* create/recreate dummy fulfill buffer of comp_data */
static int create_dummy_buffer(struct comp_data *cd, uint32_t size)
{
	if (cd == NULL) {
		trace_keydetect_dummy_error("create_dummy_buffer() "
			"error: invalid cd");
		return -EINVAL;
	}

	if (cd->fulfill_buff) {
		rfree(cd->fulfill_buff);
		cd->fulfill_buff = NULL;
	}

	if (size == 0)
		return 0;
	cd->fulfill_buff = rzalloc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM,
		size);
	if (cd->fulfill_buff == NULL) {
		trace_keydetect_dummy_error("create_dummy_buffer()"
			"alloc failed");
		return -ENOMEM;
	}

	return 0;
}

/* free dummy fulfill buffer of comp_data */
static int free_dummy_buffer(struct comp_data *cd)
{
	if (cd == NULL) {
		trace_keydetect_dummy_error("free_dummy_buffer() "
			"error: invalid cd");
		return -EINVAL;
	}

	if (cd->fulfill_buff) {
		rfree(cd->fulfill_buff);
		cd->fulfill_buff = NULL;
	}

	return 0;
}

static void keydetect_dummy_free_parameters(
	struct sof_keydetect_dummy_config **config)
{
	rfree(*config);
	*config = NULL;
}

static int keydetect_dummy_setup(struct comp_data *cd)
{
	struct sof_keydetect_dummy_config *config = cd->config;
	int ret = 0;

	ret = create_dummy_buffer(cd, config->fulfill_buf_size);

	return ret;
}

static int keydetect_dummy_cmd_set_data(struct comp_dev *dev,
	struct sof_ipc_ctrl_data *cdata)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	struct sof_keydetect_dummy_config *cfg;
	size_t bs;
	int ret = 0;

	/* Check version from ABI header */
	if (SOF_ABI_VERSION_INCOMPATIBLE(SOF_ABI_VERSION, cdata->data->abi)) {
		trace_keydetect_dummy_error("keydetect_dummy_cmd_set_data() "
			"error: invalid version");
		return -EINVAL;
	}

	switch (cdata->cmd) {
	case SOF_CTRL_CMD_ENUM:
		trace_keydetect_dummy("keydetect_dummy_cmd_set_data(),"
			"SOF_CTRL_CMD_ENUM");
		break;
	case SOF_CTRL_CMD_BINARY:
		trace_keydetect_dummy("keydetect_dummy_cmd_set_data(),"
			"SOF_CTRL_CMD_BINARY");

		if (dev->state != COMP_STATE_READY) {
			/* It is a valid request but currently this is not
			 * supported during playback/capture. The driver will
			 * re-send data in next resume when idle and the new
			 * configuration will be used when playback/capture
			 * starts.
			 */
			trace_keydetect_dummy_error(
				"keydetect_dummy_cmd_set_data() error: "
				"driver is busy");
			return -EBUSY;
		}
		/* Check and free old config */
		keydetect_dummy_free_parameters(&cd->config);

		/* Copy new config, find size from header */
		cfg = (struct sof_keydetect_dummy_config *)cdata->data->data;
		bs = cfg->size;
		trace_keydetect_dummy(
			"keydetect_dummy_cmd_set_data(), blob size = %u", bs);
		if (bs > SOF_KEYDETECT_DUMMY_MAX_SIZE || bs == 0) {
			trace_keydetect_dummy_error(
				"keydetect_dummy_cmd_set_data() error: "
				"invalid blob size");
			return -EINVAL;
		}

		/* Allocate and make a copy of the blob and setup IIR */
		cd->config = rzalloc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM, bs);
		if (!cd->config) {
			trace_keydetect_dummy_error(
				"keydetect_dummy_cmd_set_data() error: "
				"alloc failed");
			return -EINVAL;
		}

		/* Just copy the configurate.
		 * The keydetect_dummy will be initialized in
		 * prepare().
		 */
		memcpy(cd->config, cdata->data->data, bs);
		break;
	default:
		trace_keydetect_dummy_error(
			"keydetect_dummy_cmd_set_data() "
			"error: invalid cdata->cmd");
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int keydetect_dummy_cmd_get_data(struct comp_dev *dev,
	struct sof_ipc_ctrl_data *cdata, int max_size)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	size_t bs;
	int ret = 0;

	switch (cdata->cmd) {
	case SOF_CTRL_CMD_BINARY:
		trace_keydetect_dummy(
			"keydetect_dummy_cmd_get_data(), SOF_CTRL_CMD_BINARY");
		/* Copy back to user space */
		if (cd->config) {
			bs = cd->config->size;
			trace_value(bs);
			if (bs > SOF_KEYDETECT_DUMMY_MAX_SIZE || bs == 0 ||
				bs > max_size)
				return -EINVAL;
			memcpy(cdata->data->data, cd->config, bs);
			cdata->data->abi = SOF_ABI_VERSION;
			cdata->data->size = bs;
		} else {
			trace_keydetect_dummy_error(
				"keydetect_dummy_cmd_get_data() error: "
				"invalid cd->config");
			ret = -EINVAL;
		}
		break;
	default:
		trace_keydetect_dummy_error(
			"keydetect_dummy_cmd_get_data() "
			"error: invalid cdata->cmd");
		return -EINVAL;
	}

	return ret;
}

static int keydetect_dummy_cmd_set_value(struct comp_dev *dev,
	struct sof_ipc_ctrl_data *cdata)
{
	struct comp_data *cd = comp_get_drvdata(dev);

	switch (cdata->cmd) {
	case SOF_CTRL_CMD_SWITCH:
		trace_keydetect_dummy(
			"keydetect_dummy_cmd_set_value(), "
			"SOF_CTRL_CMD_SWITCH, cdata->comp_id = %u, uvalue = %u",
			cdata->comp_id, cdata->compv[0].uvalue);
		cd->tmp_level = cdata->compv[0].uvalue;
		break;
	default:
		trace_keydetect_dummy_error(
			"keydetect_dummy_cmd_set_value() "
			"error: invalid cdata->cmd");
		return -EINVAL;
	}

	return 0;
}

static int keydetect_dummy_cmd_get_value(struct comp_dev *dev,
	struct sof_ipc_ctrl_data *cdata, int size)
{
	struct comp_data *cd = comp_get_drvdata(dev);

	trace_keydetect_dummy("keydetect_dummy_cmd_get_value()");

	if (cdata->cmd == SOF_CTRL_CMD_SWITCH) {
		trace_keydetect_dummy("keydetect_dummy_cmd_get_value(), "
			"SOF_CTRL_CMD_SWITCH, cdata->comp_id = %u",
			cdata->comp_id);
		cdata->compv[0].index = 0;
		cdata->compv[0].uvalue = cd->tmp_level;
		trace_keydetect_dummy("keydetect_dummy_cmd_get_value(), "
			"index = %u, uvalue = %u",
			cdata->compv[0].index,
			cdata->compv[0].uvalue);
	} else {
		trace_keydetect_dummy_error("keydetect_dummy_cmd_get_value() "
			"error: invalid cdata->cmd");
		return -EINVAL;
	}
	return 0;
}

static struct comp_dev *keydetect_dummy_new(struct sof_ipc_comp *comp)
{
	struct comp_dev *dev;
	struct sof_ipc_comp_keydetect_dummy *keydetect_dummy;
	struct sof_ipc_comp_keydetect_dummy *ipc_keydetect_dummy =
		(struct sof_ipc_comp_keydetect_dummy *)comp;
	struct comp_data *cd;

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

	cd->tmp_level = 0;
	cd->fulfill_buff = NULL;

	comp_set_drvdata(dev, cd);
	dev->state = COMP_STATE_READY;

	return dev;
}

static void keydetect_dummy_free(struct comp_dev *dev)
{
	struct comp_data *cd = comp_get_drvdata(dev);

	trace_keydetect_dummy("keydetect_dummy_free()");

	free_dummy_buffer(cd);
	rfree(cd);
	rfree(dev);
}

/* set component audio stream parameters */
static int keydetect_dummy_params(struct comp_dev *dev)
{
	trace_keydetect_dummy("keydetect_dummy_params()");

	return 0;
}

/* used to pass standard and bespoke commands (with data) to component */
static int keydetect_dummy_cmd(struct comp_dev *dev, int cmd, void *data,
		   int max_data_size)
{
	struct sof_ipc_ctrl_data *cdata = data;
	int ret = 0;

	trace_keydetect_dummy("keydetect_dummy_cmd()");

	switch (cmd) {
	case COMP_CMD_SET_DATA:
		ret = keydetect_dummy_cmd_set_data(dev, cdata);
		break;
	case COMP_CMD_GET_DATA:
		ret = keydetect_dummy_cmd_get_data(dev, cdata, max_data_size);
		break;
	case COMP_CMD_SET_VALUE:
		ret = keydetect_dummy_cmd_set_value(dev, cdata);
		break;
	case COMP_CMD_GET_VALUE:
		ret = keydetect_dummy_cmd_get_value(dev, cdata, max_data_size);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

/* used to pass standard and bespoke commands (with data) to component */
static int keydetect_dummy_trigger(struct comp_dev *dev, int cmd)
{
	trace_keydetect_dummy("keydetect_dummy_trigger()");

	return comp_set_state(dev, cmd);
}

/* copy and process stream data from source to sink buffers */
static int keydetect_dummy_copy(struct comp_dev *dev)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	struct comp_buffer *sink;
	struct comp_buffer *source;

	tracev_keydetect_dummy("keydetect_dummy_copy()");

	/* keydetect_dummy components
	 * will only ever have 1 source and 1 sink buffer
	 */
	source = list_first_item(&dev->bsource_list,
		struct comp_buffer, sink_list);
	sink = list_first_item(&dev->bsink_list,
		struct comp_buffer, source_list);

	/* make sure source component buffer has enough data available and that
	 * the sink component buffer has enough free bytes for copy. Also
	 * check for XRUNs
	 */
	if (source->avail < cd->source_period_bytes) {
		trace_keydetect_dummy_error("keydetect_dummy_copy() error: "
			"source component buffer"
			" has not enough data available");
		comp_underrun(dev, source, cd->source_period_bytes, 0);
		return -EIO;	/* xrun */
	}
	if (sink->free < cd->sink_period_bytes) {
		trace_keydetect_dummy_error("keydetect_dummy_copy() error: "
			"sink component buffer"
			" has not enough free bytes for copy");
		comp_overrun(dev, sink, cd->sink_period_bytes, 0);
		return -EIO;	/* xrun */
	}

	cd->main_dummy_func(dev, sink, source);

	/* calc new free and available */
	comp_update_buffer_produce(sink, cd->sink_period_bytes);
	comp_update_buffer_consume(source, cd->source_period_bytes);

	return dev->frames;
}

static int keydetect_dummy_reset(struct comp_dev *dev)
{
	trace_keydetect_dummy("keydetect_dummy_reset()");

	comp_set_state(dev, COMP_TRIGGER_RESET);
	return 0;
}

static int keydetect_dummy_prepare(struct comp_dev *dev)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	struct comp_buffer *sinkb;
	struct comp_buffer *sourceb;
	struct sof_ipc_comp_config *config = COMP_GET_CONFIG(dev);
	int ret;

	trace_keydetect_dummy("keydetect_dummy_prepare()");

	ret = comp_set_state(dev, COMP_TRIGGER_PREPARE);
	if (ret < 0)
		return ret;

	if (cd->config) {
		ret = keydetect_dummy_setup(cd);
		if (ret < 0) {
			trace_keydetect_dummy_error(
				"keydetect_dummy_prepare() error: "
				"keydetect_dummy_setup failed.");
			comp_set_state(dev, COMP_TRIGGER_RESET);
			return ret;
		}
		trace_keydetect_dummy(
			"keydetect_dummy_prepare(), "
			"fulfill_buf_size = %u, mips = %u",
			cd->config->fulfill_buf_size, cd->config->mips);
	} else {
		trace_keydetect_dummy_error("keydetect_dummy_prepare() error: "
			"config not set.");
		return -EINVAL;
	}

	cd->main_dummy_func = main_dummy_function;

	/* keydetect_dummy components
	 * will only ever have 1 source and 1 sink buffer
	 */
	sourceb = list_first_item(&dev->bsource_list,
		struct comp_buffer, sink_list);
	sinkb = list_first_item(&dev->bsink_list,
		struct comp_buffer, source_list);

	/* get source data format */
	comp_set_period_bytes(sourceb->source, dev->frames, &cd->source_format,
		&cd->source_period_bytes);

	/* get sink data format */
	comp_set_period_bytes(sinkb->sink, dev->frames, &cd->sink_format,
		&cd->sink_period_bytes);


	/* rewrite params format for all downstream */
	dev->params.frame_fmt = cd->sink_format;

	dev->frame_bytes = cd->sink_period_bytes / dev->frames;

	/* set downstream buffer size */
	ret = buffer_set_size(sinkb, cd->sink_period_bytes *
		config->periods_sink);
	if (ret < 0) {
		trace_keydetect_dummy_error("keydetect_dummy_prepare() error: "
			"buffer_set_size() failed");
		goto err;
	}

	/* validate */
	if (cd->sink_period_bytes == 0) {
		trace_keydetect_dummy_error("keydetect_dummy_prepare() error: "
			"cd->sink_period_bytes = 0, dev->frames ="
			" %u, sinkb->sink->frame_bytes = %u",
			dev->frames, sinkb->sink->frame_bytes);
		ret = -EINVAL;
		goto err;
	}
	if (cd->source_period_bytes == 0) {
		trace_keydetect_dummy_error("keydetect_dummy_prepare() error: "
			"cd->source_period_bytes = 0, "
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
}

/**
 * \brief Executes cache operation on keydetect_dummy component.
 * \param[in,out] dev keydetect_dummy base component device.
 * \param[in] cmd Cache command.
 */
static void keydetect_dummy_cache(struct comp_dev *dev, int cmd)
{
	struct comp_data *cd;

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
