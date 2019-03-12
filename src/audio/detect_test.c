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
	enum sof_ipc_frame source_format;	/**< source frame format */
	uint32_t period_bytes;		/**< source number of period bytes */

	struct sof_detect_test_config *config;
	void *load_memory;	/**< synthetic memory load */
	int32_t prev_sample;	/**< last samples from previous period */
	uint32_t detected;

	void (*detect_func)(struct comp_dev *dev,
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

static void default_detect_test(struct comp_dev *dev,
				struct comp_buffer *source, uint32_t frames)
{
	struct comp_data *cd = comp_get_drvdata(dev);

	int32_t *src = source->r_ptr;
	uint32_t count = frames; /**< Assmuming single channel */
	uint32_t sample;

	/* synthetic load */
	if (cd->config)
		idelay(cd->config->load_mips * 1000000);

	/* compare with the last sample from previous period */
	if (!cd->detected && abs(cd->prev_sample - src[0]) >=
	    DETECT_TEST_SPIKE_THRESHOLD) {
		detect_test_notify(dev);
		cd->detected = 1;
	}

	/* perform detection within current period */
	for (sample = 1; sample < count; ++sample) {
		if (!cd->detected &&
		    abs(src[sample - 1] - src[sample]) >=
		    DETECT_TEST_SPIKE_THRESHOLD) {
			detect_test_notify(dev);
			cd->detected = 1;
		}
	}

	/* remember last sample from the current period */
	cd->prev_sample = src[count - 1];
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

	if (!cd->load_memory) {
		trace_keyword_error("alloc_mem_load() alloc failed");
		return -ENOMEM;
	}

	bzero(cd->load_memory, size);

	return 0;
}

static void detect_test_free_parameters(struct comp_data *cd)
{
	rfree(cd->config);
	cd->config = NULL;
}

static struct comp_dev *test_keyword_new(struct sof_ipc_comp *comp)
{
	struct comp_dev *dev;
	struct sof_ipc_comp_process *keyword;
	struct sof_ipc_comp_process *ipc_keyword =
		(struct sof_ipc_comp_process *)comp;
	struct comp_data *cd;

	trace_keyword("test_keyword_new()");

	if (IPC_IS_SIZE_INVALID(ipc_keyword->config)) {
		IPC_SIZE_ERROR_TRACE(TRACE_CLASS_KEYWORD, ipc_keyword->config);
		return NULL;
	}

	dev = rzalloc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM,
		      COMP_SIZE(struct sof_ipc_comp_process));
	if (!dev)
		return NULL;

	keyword = (struct sof_ipc_comp_process *)&dev->comp;
	memcpy(keyword, ipc_keyword, sizeof(struct sof_ipc_comp_process));

	cd = rzalloc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM, sizeof(*cd));

	if (!cd) {
		rfree(dev);
		return NULL;
	}

	/* using default processing function */
	cd->detect_func = default_detect_test;

	comp_set_drvdata(dev, cd);
	dev->state = COMP_STATE_READY;
	return dev;
}

static void test_keyword_free(struct comp_dev *dev)
{
	struct comp_data *cd = comp_get_drvdata(dev);

	trace_keyword("test_keyword_free()");

	detect_test_free_parameters(cd);
	free_mem_load(cd);
	rfree(cd);
	rfree(dev);
}

/* set component audio stream parameters */
static int test_keyword_params(struct comp_dev *dev)
{
	struct comp_data *cd = comp_get_drvdata(dev);

	if (dev->params.channels != 1) {
		trace_keyword_error("test_keyword_params() "
				    "error: only single-channel supported");
		return -EINVAL;
	}

	dev->frame_bytes = comp_frame_bytes(dev);

	/* calculate period size based on config */
	cd->period_bytes = dev->frames * dev->frame_bytes;

	return 0;
}

static int test_keyword_ctrl_set_data(struct comp_dev *dev,
				      struct sof_ipc_ctrl_data *cdata)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	struct sof_detect_test_config *cfg;
	size_t bs;
	int ret = 0;

	/* Check version from ABI header */
	if (SOF_ABI_VERSION_INCOMPATIBLE(SOF_ABI_VERSION, cdata->data->abi)) {
		trace_keyword_error("test_keyword_cmd_set_data() error: "
				    "invalid version");
		return -EINVAL;
	}

	switch (cdata->cmd) {
	case SOF_CTRL_CMD_ENUM:
		trace_keyword("test_keyword_cmd_set_data(), SOF_CTRL_CMD_ENUM");
		break;
	case SOF_CTRL_CMD_BINARY:
		trace_keyword("test_keyword_cmd_set_data(), "
			      "SOF_CTRL_CMD_BINARY");

		if (dev->state != COMP_STATE_READY) {
			/* It is a valid request but currently this is not
			 * supported during playback/capture. The driver will
			 * re-send data in next resume when idle and the new
			 * configuration will be used when playback/capture
			 * starts.
			 */
			trace_keyword_error("test_keyword_cmd_set_data() "
					    "error: driver is busy");
			return -EBUSY;
		}
		/* Check and free old config */
		detect_test_free_parameters(cd);

		/* Copy new config, find size from header */
		cfg = (struct sof_detect_test_config *)cdata->data->data;
		bs = cfg->size;

		trace_keyword("test_keyword_cmd_set_data(), blob size = %u",
			      bs);

		if (bs > SOF_DETECT_TEST_MAX_CFG_SIZE || bs == 0) {
			trace_keyword_error("test_keyword_cmd_set_data() "
					    "error: invalid blob size");
			return -EINVAL;
		}

		/* Allocate and make a copy of the blob
		 * and setup the configuration
		 */
		cd->config = rzalloc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM, bs);
		if (!cd->config) {
			trace_keyword_error("test_keyword_cmd_set_data() "
					    "error: alloc failed");
			return -EINVAL;
		}

		/* Just copy the configuration.
		 * The component will be initialized in prepare().
		 */
		memcpy(cd->config, cdata->data->data, bs);
		break;
	default:
		trace_keyword_error("test_keyword_cmd_set_data() "
				    "error: invalid cdata->cmd");
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int test_keyword_ctrl_get_data(struct comp_dev *dev,
				      struct sof_ipc_ctrl_data *cdata, int size)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	size_t bs;
	int ret = 0;

	switch (cdata->cmd) {
	case SOF_CTRL_CMD_BINARY:
		trace_keyword("test_keyword_ctrl_get_data(), "
			      "SOF_CTRL_CMD_BINARY");
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
			trace_keyword_error("test_keyword_ctrl_get_data() "
					    "error: invalid cd->config");
			ret = -EINVAL;
		}
		break;
	default:
		trace_keyword_error("test_keyword_ctrl_get_data() error: "
				    "invalid cdata->cmd");
		return -EINVAL;
	}

	return ret;
}

/* used to pass standard and bespoke commands (with data) to component */
static int test_keyword_cmd(struct comp_dev *dev, int cmd, void *data,
			    int max_data_size)
{
	struct sof_ipc_ctrl_data *cdata = data;

	trace_keyword("test_keyword_cmd()");

	switch (cmd) {
	case COMP_CMD_SET_DATA:
		return test_keyword_ctrl_set_data(dev, cdata);
	case COMP_CMD_GET_DATA:
		return test_keyword_ctrl_get_data(dev, cdata, max_data_size);
	default:
		return -EINVAL;
	}
}

static int test_keyword_trigger(struct comp_dev *dev, int cmd)
{
	int ret;
	struct comp_data *cd = comp_get_drvdata(dev);

	trace_keyword("test_keyword_trigger()");

	ret = comp_set_state(dev, cmd);
	if (ret)
		return ret;

	switch (cmd) {
	case COMP_TRIGGER_START:
	case COMP_TRIGGER_RELEASE:
		cd->detected = 0;
		break;
	}

	return ret;
}

/*  process stream data from source buffer */
static int test_keyword_copy(struct comp_dev *dev)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	struct comp_buffer *source;

	tracev_keyword("test_keyword_copy()");

	/* keyword components will only ever have 1 source */
	source = list_first_item(&dev->bsource_list,
				 struct comp_buffer, sink_list);

	/* make sure source component buffer has enough data available for copy
	 * Also check for XRUNs
	 */
	if (source->avail < cd->period_bytes) {
		trace_keyword_error("test_keyword_copy() error: "
				   "source component buffer"
				   " has not enough data available");
		comp_underrun(dev, source, cd->period_bytes, 0);
		return -EIO;	/* xrun */
	}

	/* copy and perform detection */
	cd->detect_func(dev, source, dev->frames);

	/* calc new available */
	comp_update_buffer_consume(source, cd->period_bytes);

	return dev->frames;
}

static int test_keyword_reset(struct comp_dev *dev)
{
	trace_keyword("test_keyword_reset()");

	return comp_set_state(dev, COMP_TRIGGER_RESET);
}

static int test_keyword_prepare(struct comp_dev *dev)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	int ret;

	trace_keyword("test_keyword_prepare()");

	ret = comp_set_state(dev, COMP_TRIGGER_PREPARE);
	if (ret)
		return ret;

	if (cd->config) {
		ret = alloc_mem_load(cd, cd->config->load_memory_size);
		if (ret < 0)
			goto err;
	}

	return 0;

err:
	comp_set_state(dev, COMP_TRIGGER_RESET);
	return ret;
}

static void test_keyword_cache(struct comp_dev *dev, int cmd)
{
	struct comp_data *cd;

	switch (cmd) {
	case CACHE_WRITEBACK_INV:
		trace_keyword("test_keyword_cache(), CACHE_WRITEBACK_INV");

		cd = comp_get_drvdata(dev);

		dcache_writeback_invalidate_region(cd, sizeof(*cd));
		dcache_writeback_invalidate_region(dev, sizeof(*dev));
		break;

	case CACHE_INVALIDATE:
		trace_keyword("test_keyword_cache(), CACHE_INVALIDATE");

		dcache_invalidate_region(dev, sizeof(*dev));

		cd = comp_get_drvdata(dev);
		dcache_invalidate_region(cd, sizeof(*cd));
		break;
	}
}

struct comp_driver comp_keyword = {
	.type	= SOF_COMP_KEYWORD_DETECT,
	.ops	= {
		.new		= test_keyword_new,
		.free		= test_keyword_free,
		.params		= test_keyword_params,
		.cmd		= test_keyword_cmd,
		.trigger	= test_keyword_trigger,
		.copy		= test_keyword_copy,
		.prepare	= test_keyword_prepare,
		.reset		= test_keyword_reset,
		.cache		= test_keyword_cache,
	},
};

void sys_comp_keyword_init(void);

void sys_comp_keyword_init(void)
{
	comp_register(&comp_keyword);
}

DECLARE_COMPONENT(sys_comp_keyword_init);
