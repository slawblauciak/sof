/*
 * Copyright (c) 2019, Intel Corporation
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
 * Author: Slawomir Blauciak <slawomir.blauciak@linux.intel.com>
 */

#include <stdint.h>
#include <stddef.h>
#include <sof/lock.h>
#include <sof/list.h>
#include <sof/stream.h>
#include <sof/ipc.h>
#include <sof/audio/component.h>
#include <uapi/user/detect_test.h>

/* tracing */
#define trace_keyword(__e, ...) \
	trace_event(TRACE_CLASS_KEYWORD, __e, ##__VA_ARGS__)
#define trace_keyword_error(__e, ...) \
	trace_error(TRACE_CLASS_KEYWORD, __e, ##__VA_ARGS__)
#define tracev_keyword(__e, ...) \
	tracev_event(TRACE_CLASS_KEYWORD, __e, ##__VA_ARGS__)

#define DETECT_TEST_SPIKE_THRESHOLD 0x00FFFFFF

struct comp_data {
	uint32_t source_period_bytes;	/**< source number of period bytes */
	uint32_t sink_period_bytes;	/**< sink number of period bytes */
	enum sof_ipc_frame source_format;	/**< source frame format */
	enum sof_ipc_frame sink_format;	/**< sink frame format */
	uint32_t period_bytes;
	uint32_t switch_state[2];

	struct sof_detect_test_config *config;
	void *load_memory;	/**< synthetic memory load */
	int32_t *prev_samples;	/**< last samples from previous period */

	void (*detect_func)(struct comp_dev *dev, struct comp_buffer *sink,
			    struct comp_buffer *source, uint32_t frames);
};

static void detect_test_notify(struct comp_dev *dev)
{
	struct sof_ipc_comp_event event;

	trace_keyword("detect_test_notify()");

	event.event_type = SOF_CTRL_EVENT_KD;
	event.num_elems = 0;

	ipc_send_comp_notification(dev, &event);
}

static void default_detect_test(struct comp_dev *dev, struct comp_buffer *sink,
				struct comp_buffer *source, uint32_t frames)
{
	struct comp_data *cd = comp_get_drvdata(dev);

	int32_t *src = source->r_ptr;
	int32_t *dest = sink->w_ptr;
	uint32_t count = frames * dev->params.channels;
	uint32_t sample;
	int notified = 0;

	/* synthetic load */
	if (cd->config)
		idelay(cd->config->load_mips * 1000000);

	/* compare with samples from previous period */
	for (sample = 0; sample < dev->params.channels; ++sample) {
		if (abs(cd->prev_samples[sample] - src[sample]) >=
		    DETECT_TEST_SPIKE_THRESHOLD) {
			detect_test_notify(dev);
			notified = 1;
		}
	}

	/* copy the samples and perform detection within current period */
	for (sample = 0; sample < count; ++sample) {
		dest[sample] = src[sample];

		if (!notified && sample >= dev->params.channels &&
		    abs(src[sample - dev->params.channels] - src[sample]) >=
		    DETECT_TEST_SPIKE_THRESHOLD) {
			detect_test_notify(dev);
			notified = 1;
		}
	}

	/* remember last samples from the current period */
	for (sample = 0; sample < dev->params.channels; ++sample)
		cd->prev_samples[sample] = src[(count - dev->params.channels) +
					       sample];
}

static int free_mem_load(struct comp_data *cd)
{
	if (!cd) {
		trace_keyword_error("free_mem_load() error: invalid cd");
		return -EINVAL;
	}

	if (cd->load_memory) {
		rfree(cd->load_memory);
		cd->load_memory = NULL;
	}

	return 0;
}

static int alloc_mem_load(struct comp_data *cd, uint32_t size)
{
	if (!cd) {
		trace_keyword_error("alloc_mem_load() error: invalid cd");
		return -EINVAL;
	}

	free_mem_load(cd);

	if (!size)
		return 0;

	cd->load_memory = rballoc(RZONE_BUFFER, SOF_MEM_CAPS_RAM, size);
	bzero(cd->load_memory, size);

	if (!cd->load_memory) {
		trace_keyword_error("alloc_mem_load() alloc failed");
		return -ENOMEM;
	}

	return 0;
}

static void detect_test_free_parameters(struct comp_data *cd)
{
	rfree(cd->config);
	cd->config = NULL;
}

static void detect_test_free_buffers(struct comp_data *cd)
{
	rfree(cd->prev_samples);
	cd->prev_samples = NULL;
}

static struct comp_dev *keyword_new(struct sof_ipc_comp *comp)
{
	struct comp_dev *dev;
	struct sof_ipc_comp_process *keyword;
	struct sof_ipc_comp_process *ipc_keyword =
		(struct sof_ipc_comp_process *)comp;
	struct comp_data *kd;

	trace_keyword("keyword_new()");

	if (IPC_IS_SIZE_INVALID(ipc_keyword->config)) {
		IPC_SIZE_ERROR_TRACE(TRACE_CLASS_MUX, ipc_keyword->config);
		return NULL;
	}

	dev = rzalloc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM,
		      COMP_SIZE(struct sof_ipc_comp_process));
	if (!dev)
		return NULL;

	keyword = (struct sof_ipc_comp_process *)&dev->comp;
	memcpy(keyword, ipc_keyword, sizeof(struct sof_ipc_comp_process));

	kd = rzalloc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM, sizeof(*kd));

	if (!kd) {
		rfree(dev);
		return NULL;
	}

	comp_set_drvdata(dev, kd);
	dev->state = COMP_STATE_READY;
	return dev;
}

static void keyword_free(struct comp_dev *dev)
{
	struct comp_data *cd = comp_get_drvdata(dev);

	trace_keyword("keyword_free()");

	detect_test_free_parameters(cd);
	detect_test_free_buffers(cd);
	free_mem_load(cd);
	rfree(cd);
	rfree(dev);
}

/* set component audio stream parameters */
static int keyword_params(struct comp_dev *dev)
{
	return 0;
}

/**
 * \brief Sets keyword control command.
 * \param[in,out] dev keyword base component device.
 * \param[in,out] cdata Control command data.
 * \return Error code.
 */
static int keyword_ctrl_set_value(struct comp_dev *dev,
				  struct sof_ipc_ctrl_data *cdata)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	int j;

	/* validate */
	if (cdata->num_elems == 0 || cdata->num_elems > SOF_IPC_MAX_CHANNELS) {
		trace_keyword_error("keyword_ctrl_set_value() error: "
				   "invalid cdata->num_elems");
		return -EINVAL;
	}

	switch (cdata->cmd) {
	case SOF_CTRL_CMD_SWITCH:
		/*FIXME: fix logger for keywork */
		trace_keyword("keyword_ctrl_set_value(), "
			      "SOF_CTRL_CMD_SWITCH, ");

		/* save switch state */
		for (j = 0; j < cdata->num_elems; j++)
			cd->switch_state[j] = cdata->chanv[j].value;

		break;

	default:
		trace_keyword_error("keyword_ctrl_set_value() error: "
				   "invalid cdata->cmd");
		return -EINVAL;
	}

	return 0;
}

/**
 * \brief Gets keyword control command.
 * \param[in,out] dev keyword base component device.
 * \param[in,out] cdata Control command data.
 * \return Error code.
 */
static int keyword_ctrl_get_value(struct comp_dev *dev,
				  struct sof_ipc_ctrl_data *cdata, int size)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	int j;

	/* validate */
	if (cdata->num_elems == 0 || cdata->num_elems > SOF_IPC_MAX_CHANNELS) {
		trace_keyword_error("invalid cdata->num_elems");
		return -EINVAL;
	}

	if (cdata->cmd ==  SOF_CTRL_CMD_SWITCH) {
		trace_keyword("keyword_ctrl_get_value(), SOF_CTRL_CMD_SWITCH");

		/* copy current switch state */
		for (j = 0; j < cdata->num_elems; j++) {
			cdata->chanv[j].channel = j;
			cdata->chanv[j].value = cd->switch_state[j];
		}
	} else {
		trace_keyword_error("invalid cdata->cmd");
		return -EINVAL;
	}

	return 0;
}

static int keyword_ctrl_set_data(struct comp_dev *dev,
				 struct sof_ipc_ctrl_data *cdata)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	struct sof_detect_test_config *cfg;
	size_t bs;
	int ret = 0;

	/* Check version from ABI header */
	if (SOF_ABI_VERSION_INCOMPATIBLE(SOF_ABI_VERSION, cdata->data->abi)) {
		trace_keyword_error("keyword_cmd_set_data() error: "
				    "invalid version");
		return -EINVAL;
	}

	switch (cdata->cmd) {
	case SOF_CTRL_CMD_ENUM:
		trace_keyword("keyword_cmd_set_data(), SOF_CTRL_CMD_ENUM");
		break;
	case SOF_CTRL_CMD_BINARY:
		trace_keyword("keyword_cmd_set_data(), SOF_CTRL_CMD_BINARY");

		if (dev->state != COMP_STATE_READY) {
			/* It is a valid request but currently this is not
			 * supported during playback/capture. The driver will
			 * re-send data in next resume when idle and the new
			 * configuration will be used when playback/capture
			 * starts.
			 */
			trace_keyword_error("keyword_cmd_set_data() error: "
					    "driver is busy");
			return -EBUSY;
		}
		/* Check and free old config */
		detect_test_free_parameters(cd);

		/* Copy new config, find size from header */
		cfg = (struct sof_detect_test_config *)cdata->data->data;
		bs = cfg->size;

		trace_keyword("keyword_cmd_set_data(), blob size = %u", bs);

		if (bs > SOF_DETECT_TEST_MAX_CFG_SIZE || bs == 0) {
			trace_keyword_error("keyword_cmd_set_data() error: "
					    "invalid blob size");
			return -EINVAL;
		}

		/* Allocate and make a copy of the blob and setup IIR */
		cd->config = rzalloc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM, bs);
		if (!cd->config) {
			trace_keyword_error("keyword_cmd_set_data() error: "
					    "alloc failed");
			return -EINVAL;
		}

		/* Just copy the configuration.
		 * The component will be initialized in prepare().
		 */
		memcpy(cd->config, cdata->data->data, bs);
		break;
	default:
		trace_keyword_error("keyword_cmd_set_data() "
				    "error: invalid cdata->cmd");
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int keyword_ctrl_get_data(struct comp_dev *dev,
				 struct sof_ipc_ctrl_data *cdata, int size)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	size_t bs;
	int ret = 0;

	switch (cdata->cmd) {
	case SOF_CTRL_CMD_BINARY:
		trace_keyword("keyword_ctrl_get_data(), SOF_CTRL_CMD_BINARY");
		/* Copy back to user space */
		if (cd->config) {
			bs = cd->config->size;
			trace_value(bs);

			if (bs == 0 || bs > size)
				return -EINVAL;

			memcpy(cdata->data->data, cd->config, bs);
			cdata->data->abi = SOF_ABI_VERSION;
			cdata->data->size = bs;
		} else {
			trace_keyword_error("keyword_ctrl_get_data() error: "
					    "invalid cd->config");
			ret = -EINVAL;
		}
		break;
	default:
		trace_keyword_error("keyword_ctrl_get_data() error: "
				    "invalid cdata->cmd");
		return -EINVAL;
	}

	return ret;
}

/* used to pass standard and bespoke commands (with data) to component */
static int keyword_cmd(struct comp_dev *dev, int cmd, void *data,
		   int max_data_size)
{
	struct sof_ipc_ctrl_data *cdata = data;

	trace_keyword("keyword_cmd()");

	switch (cmd) {
	case COMP_CMD_SET_VALUE:
		return keyword_ctrl_set_value(dev, cdata);
	case COMP_CMD_GET_VALUE:
		return keyword_ctrl_get_value(dev, cdata, max_data_size);
	case COMP_CMD_SET_DATA:
		return keyword_ctrl_set_data(dev, cdata);
	case COMP_CMD_GET_DATA:
		return keyword_ctrl_get_data(dev, cdata, max_data_size);
	default:
		return -EINVAL;
	}
}

static int keyword_trigger(struct comp_dev *dev, int cmd)
{
	trace_keyword("keyword_trigger()");

	return comp_set_state(dev, cmd);
}

/* copy and process stream data from source to sink buffers */
static int keyword_copy(struct comp_dev *dev)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	struct comp_buffer *sink;
	struct comp_buffer *source;

	tracev_keyword("keyword_copy()");

	/* keyword components will only ever have 1 source and 1 sink buffer */
	source = list_first_item(&dev->bsource_list,
				 struct comp_buffer, sink_list);
	sink = list_first_item(&dev->bsink_list,
			       struct comp_buffer, source_list);

	/* make sure source component buffer has enough data available and that
	 * the sink component buffer has enough free bytes for copy. Also
	 * check for XRUNs
	 */
	if (source->avail < cd->source_period_bytes) {
		trace_keyword_error("keyword_copy() error: "
				   "source component buffer"
				   " has not enough data available");
		comp_underrun(dev, source, cd->source_period_bytes, 0);
		return -EIO;	/* xrun */
	}
	if (sink->free < cd->sink_period_bytes) {
		trace_keyword_error("keyword_copy() error: "
				   "sink component buffer"
				   " has not enough free bytes for copy");
		comp_overrun(dev, sink, cd->sink_period_bytes, 0);
		return -EIO;	/* xrun */
	}

	/* copy and perform detection */
	cd->detect_func(dev, sink, source, dev->frames);

	/* calc new free and available */
	comp_update_buffer_produce(sink, cd->sink_period_bytes);
	comp_update_buffer_consume(source, cd->source_period_bytes);

	return dev->frames;
}

static int keyword_reset(struct comp_dev *dev)
{
	trace_keyword("keyword_reset()");

	comp_set_state(dev, COMP_TRIGGER_RESET);
	return 0;
}

static int keyword_prepare(struct comp_dev *dev)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	struct comp_buffer *sinkb;
	struct comp_buffer *sourceb;
	struct sof_ipc_comp_config *config = COMP_GET_CONFIG(dev);
	int ret;

	trace_keyword("keyword_prepare()");

	ret = comp_set_state(dev, COMP_TRIGGER_PREPARE);
	if (ret)
		return ret;

	cd->detect_func = default_detect_test;
	if (cd->config) {
		ret = alloc_mem_load(cd, cd->config->load_memory_size);
		if (ret < 0)
			goto err;
	}

	detect_test_free_buffers(cd);
	cd->prev_samples = rzalloc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM,
				   sizeof(int32_t) * dev->params.channels);

	if (!cd->prev_samples) {
		trace_keyword("keyword_prepare() error: "
			      "failed to allocate sample buffer");
		goto err;
	}

	/* keyword components will only ever have 1 source and 1 sink buffer */
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
		trace_keyword_error("keyword_prepare() error: "
				   "buffer_set_size() failed");
		goto err;
	}

	/* validate */
	if (cd->sink_period_bytes == 0) {
		trace_keyword_error("keyword_prepare() error: "
				   "cd->sink_period_bytes = 0, dev->frames ="
				   " %u, sinkb->sink->frame_bytes = %u",
				   dev->frames, sinkb->sink->frame_bytes);
		ret = -EINVAL;
		goto err;
	}
	if (cd->source_period_bytes == 0) {
		trace_keyword_error("keyword_prepare() error: "
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

static void keyword_cache(struct comp_dev *dev, int cmd)
{
	struct comp_data *cd;

	switch (cmd) {
	case CACHE_WRITEBACK_INV:
		trace_keyword("keyword_cache(), CACHE_WRITEBACK_INV");

		cd = comp_get_drvdata(dev);

		dcache_writeback_invalidate_region(cd, sizeof(*cd));
		dcache_writeback_invalidate_region(dev, sizeof(*dev));
		break;

	case CACHE_INVALIDATE:
		trace_keyword("keyword_cache(), CACHE_INVALIDATE");

		dcache_invalidate_region(dev, sizeof(*dev));

		cd = comp_get_drvdata(dev);
		dcache_invalidate_region(cd, sizeof(*cd));
		break;
	}
}

struct comp_driver comp_keyword = {
	.type	= SOF_COMP_KEYWORD_DETECT,
	.ops	= {
		.new		= keyword_new,
		.free		= keyword_free,
		.params		= keyword_params,
		.cmd		= keyword_cmd,
		.trigger	= keyword_trigger,
		.copy		= keyword_copy,
		.prepare	= keyword_prepare,
		.reset		= keyword_reset,
		.cache		= keyword_cache,
	},
};

void sys_comp_keyword_init(void)
{
	comp_register(&comp_keyword);
}
