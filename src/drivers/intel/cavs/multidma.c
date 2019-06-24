// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2019 Intel Corporation. All rights reserved.
//
// Author: Slawomir Blauciak <slawomir.blauciak@linux.intel.com>

#include <sof/audio/component.h>
#include <sof/drivers/multidma.h>
#include <sof/lib/dai.h>
#include <sof/trace/trace.h>
#include <user/trace.h>
#include <stdint.h>

#define trace_multidma(__e, ...) \
	trace_event(TRACE_CLASS_DMA, __e, ##__VA_ARGS__)
#define tracev_multidma(__e, ...) \
	tracev_event(TRACE_CLASS_DMA, __e, ##__VA_ARGS__)
#define trace_multidma_error(__e, ...) \
	trace_error(TRACE_CLASS_DMA, __e, ##__VA_ARGS__)

static void multidma_callback(void *data, uint32_t type,
			      struct dma_cb_data *next)
{
	struct dma_chan_data *chan = data;
	struct multidma_chan_data *chdata = dma_chan_get_data(chan);
	struct dma_cb_data next_data = {
		.elem = { .size = chdata->last_copy_bytes }
	};

	next->status = DMA_CB_STATUS_RELOAD;
	chdata->cb_expected--;

	tracev_multidma("multidma_callback() cb_expected %u",
			chdata->cb_expected);

	if (!chdata->cb_expected)
		chan->cb(chan->cb_data, DMA_CB_TYPE_COPY, &next_data);
}

static int multidma_has_link(struct dma_chan_data *chan, uint32_t link)
{
	struct multidma_chan_data *chdata = dma_chan_get_data(chan);
	int has_link = 0;
	int i;

	for (i = 0; i < chdata->num_links; ++i) {
		if (chdata->links[i].link == link)
			has_link = 1;
	}

	return has_link;
}

static void multidma_free_links(struct dma_chan_data *chan)
{
	struct multidma_chan_data *chdata = dma_chan_get_data(chan);
	int i;

	trace_multidma("multidma_free_links(): channel %d", chan->index);

	for (i = 0; i < chdata->num_links; ++i) {
		if (chdata->links[i].channel >= 0)
			dma_channel_put(chdata->links[i].channel);

		chdata->links[i].channel = NULL;
		chdata->links[i].link = -1;
	}
}

static int multidma_start_links(struct dma_chan_data *chan)
{
	struct multidma_chan_data *chdata = dma_chan_get_data(chan);
	int i;
	int ret = 0;

	trace_multidma("multidma_start_links(): channel %d", chan->index);

	for (i = 0; i < chdata->num_links; ++i) {
		if (chdata->links[i].channel >= 0)
			ret = dma_start(chdata->links[i].channel);

		if (ret) {
			trace_multidma_error("multidma_start_links(): "
					     "failed to start link %d on "
					     "channel %d",
					     chdata->links[i].link,
					     chan->index);
			break;
		}
	}

	return ret;
}

static int multidma_stop_links(struct dma_chan_data *chan)
{
	struct multidma_chan_data *chdata = dma_chan_get_data(chan);
	int i;
	int ret = 0;
	int stop_err = 0;

	trace_multidma("multidma_stop_links(): channel %d", chan->index);

	for (i = 0; i < chdata->num_links; ++i) {
		if (chdata->links[i].channel >= 0)
			stop_err = dma_stop(chdata->links[i].channel);

		/* Attempt to stop all DMAs, even if some fail */
		if (stop_err) {
			trace_multidma_error("multidma_stop_links(): "
					     "failed to stop link %d on "
					     "channel %d",
					     chdata->links[i].link,
					     chan->index);
			ret = stop_err;
		}
	}

	/* return last dma_stop error */
	return ret;
}

static int multidma_pause_links(struct dma_chan_data *chan)
{
	struct multidma_chan_data *chdata = dma_chan_get_data(chan);
	int i;
	int ret = 0;

	trace_multidma("multidma_pause_links(): channel %d", chan->index);

	for (i = 0; i < chdata->num_links; ++i) {
		if (chdata->links[i].channel >= 0)
			ret = dma_pause(chdata->links[i].channel);

		if (ret) {
			trace_multidma_error("multidma_pause_links(): "
					     "failed to pause link %d on "
					     "channel %d",
					     chdata->links[i].link,
					     chan->index);
			break;
		}
	}

	return ret;
}

static int multidma_release_links(struct dma_chan_data *chan)
{
	struct multidma_chan_data *chdata = dma_chan_get_data(chan);
	int i;
	int ret = 0;

	trace_multidma("multidma_release_links(): channel %d", chan->index);

	for (i = 0; i < chdata->num_links; ++i) {
		if (chdata->links[i].channel >= 0)
			ret = dma_release(chdata->links[i].channel);

		if (ret) {
			trace_multidma_error("multidma_release_links(): "
					     "failed to release link %d on "
					     "channel %d",
					     chdata->links[i].link,
					     chan->index);
			break;
		}
	}

	return ret;
}

static int multidma_init_links(struct dma_chan_data *chan,
			       struct dma_sg_config *config)
{
	struct multidma_chan_data *chdata = dma_chan_get_data(chan);
	struct multidma_chan_link *link;
	struct sof_ipc_stream_map *stream_map;
	struct sof_ipc_channel_map *ch_map;
	int i;
	int cur_link_id;
	int ret = 0;

	chdata->num_links = 0;
	stream_map = config->multi.stream_map;

	for (i = 0; i < stream_map->num_ch_map; ++i) {
		ch_map = get_channel_map(stream_map, i);
		cur_link_id = ch_map->ext_id;

		if (cur_link_id < 0 ||
		    multidma_has_link(chan, cur_link_id))
			continue;

		link = &chdata->links[chdata->num_links];

		link->chdata = chdata;
		link->channel = dma_channel_get(chdata->dma, cur_link_id);
		link->link = cur_link_id;

		if (!link->channel) {
			trace_multidma_error("multidma_channel_get(): "
					     "channel %d allocation failed "
					     "(link %d)", chan->index,
					     cur_link_id);
			ret = -ENODEV;
			goto out;
		}

		ret = dma_set_cb(link->channel, DMA_CB_TYPE_COPY,
				 multidma_callback, chan);

		if (ret) {
			trace_multidma_error("multidma_channel_get(): "
					     "channel %d cb set failed "
					     "(link %d)", chan->index,
					     cur_link_id);
			goto out;
		}

		++chdata->num_links;
	}

out:
	return ret;
}

static struct dma_chan_data *multidma_channel_get(struct dma *dma,
						  unsigned int channel)
{
	struct dma_chan_data *chan;
	uint32_t lock_flags;

	if (channel >= dma->plat_data.channels) {
		trace_multidma_error("multidma_channel_get(): "
				     "invalid channel %d", channel);
		return NULL;
	}

	trace_multidma("multidma_channel_get(): channel %d", channel);

	spin_lock_irq(dma->lock, lock_flags);

	chan = &dma->chan[channel];

	if (chan->status != COMP_STATE_INIT) {
		trace_multidma_error("multidma_channel_get(): "
				     "channel %d busy", channel);
		goto fail;
	}

	chan->status = COMP_STATE_READY;
	spin_unlock_irq(dma->lock, lock_flags);
	atomic_add(&dma->num_channels_busy, 1);

	return chan;

fail:
	spin_unlock_irq(dma->lock, lock_flags);
	return NULL;
}

static void multidma_channel_put(struct dma_chan_data *chan)
{
	uint32_t lock_flags;

	trace_multidma("multidma_channel_put(): channel %d", chan->index);

	spin_lock_irq(chan->dma->lock, lock_flags);

	multidma_free_links(chan);
	chan->status = COMP_STATE_INIT;

	spin_unlock_irq(chan->dma->lock, lock_flags);
	atomic_sub(&chan->dma->num_channels_busy, 1);
}

static uint32_t multidma_copy_link_frame(struct multidma_chan_data *chdata,
					 struct multidma_chan_link *link)
{
	uint32_t roffset;
	uint32_t link_buf_size = link->elem_array.elems[0].size *
				 link->elem_array.count;
	int i;
	void *dst;

	for (i = 0; i < link->num_txforms; ++i) {
		roffset = link->roffsets[i];
		dst = (char *)link->buf_w_ptr + (i * chdata->ch_bytes);

		memcpy_s(dst, chdata->ch_bytes,
			 (char *)chdata->src_r_ptr + roffset, chdata->ch_bytes);
	}

	link->buf_w_ptr = (char *)link->buf_w_ptr +
		(link->num_txforms * chdata->ch_bytes);

	/* Destination buffer wrapping */
	if ((char *)link->buf_w_ptr >= (char *)link->buf + link_buf_size)
		link->buf_w_ptr = link->buf;

	return link->num_txforms * chdata->ch_bytes;
}

static uint32_t multidma_copy_link_bufs(struct dma_chan_data *chan,
					uint32_t flags)
{
	struct multidma_chan_data *chdata = dma_chan_get_data(chan);
	uint32_t copied = 0;
	int i;

	for (i = 0; i < chdata->num_links; ++i)
		copied += multidma_copy_link_frame(chdata, &chdata->links[i]);

	chdata->src_r_ptr = (char *)chdata->src_r_ptr + copied;

	return copied;
}

static int multidma_copy_links(struct dma_chan_data *chan, int bytes,
			       uint32_t flags)
{
	struct multidma_chan_data *chdata = dma_chan_get_data(chan);
	int i;
	int ret = 0;

	chdata->cb_expected += chdata->num_links;

	for (i = 0; i < chdata->num_links; ++i) {
		dcache_writeback_region(chdata->links[i].buf,
					chdata->src_bytes / chdata->num_links);

		ret = dma_copy(chdata->links[i].channel,
			       bytes / chdata->num_links, flags);

		if (ret < 0) {
			trace_multidma_error("multidma_copy_links(): "
					     "copy failed, link %d, ch %d",
					     chdata->links[i].link,
					     chan->index);

			--chdata->cb_expected;

			goto out;
		}
	}

out:
	return ret;
}

static int multidma_get_burst_size(struct dma_chan_data *chan)
{
	struct multidma_chan_data *chdata = dma_chan_get_data(chan);
	uint32_t burst_size = 0;
	int i;

	/* Burst size is the size of all channel transforms */
	for (i = 0; i < chdata->num_links; ++i)
		burst_size += chdata->links[i].num_txforms * chdata->ch_bytes;

	return burst_size;
}

static int multidma_copy_ch(struct dma_chan_data *chan, int bytes,
			    uint32_t flags)
{
	struct multidma_chan_data *chdata = dma_chan_get_data(chan);
	uint32_t burst_size = multidma_get_burst_size(chan);
	uint32_t bytes_until_wrap;
	uint32_t bytes_after_wrap;
	uint32_t to_copy;

	if (bytes < burst_size) {
		trace_multidma_error("multidma_copy_ch(): "
				     "data size %d not enough for "
				     "burst size %d channel %d", bytes,
				     burst_size, chan->index);
		return -EINVAL;
	}

	to_copy = bytes - (bytes % burst_size);

	tracev_multidma("multidma_copy_ch(): chan %u bytes %u to_copy %u",
			chan->index, bytes, to_copy);

	/* Source buffer wrapping */
	bytes_until_wrap = MIN(to_copy,
			       ((uint32_t)chdata->src + chdata->src_bytes) -
			       (uint32_t)chdata->src_r_ptr);
	bytes_after_wrap = to_copy - bytes_until_wrap;

	while (bytes_until_wrap)
		bytes_until_wrap -= multidma_copy_link_bufs(chan, flags);

	if ((char *)chdata->src + chdata->src_bytes <=
	    (char *)chdata->src_r_ptr)
		chdata->src_r_ptr = chdata->src;

	while (bytes_after_wrap)
		bytes_after_wrap -= multidma_copy_link_bufs(chan, flags);

	chdata->last_copy_bytes = to_copy;

	return multidma_copy_links(chan, to_copy, flags);
}

static int multidma_copy(struct dma_chan_data *chan, int bytes, uint32_t flags)
{
	uint32_t lock_flags;
	int ret = 0;

	spin_lock_irq(chan->dma->lock, lock_flags);

	ret = multidma_copy_ch(chan, bytes, flags);

	spin_unlock_irq(chan->dma->lock, lock_flags);

	return ret;
}

static int multidma_start(struct dma_chan_data *chan)
{
	struct multidma_chan_data *chdata;
	struct dma_cb_data next = { .elem = { .size = 0 } };
	uint32_t lock_flags;
	int ret;

	trace_multidma("multidma_start(): channel %d", chan->index);

	spin_lock_irq(chan->dma->lock, lock_flags);

	if (chan->status != COMP_STATE_PREPARE) {
		trace_multidma_error("multidma_start(): channel %d busy",
				     chan->index);

		ret = -EBUSY;
		goto out;
	}

	ret = multidma_start_links(chan);

	if (ret) {
		trace_multidma_error("multidma_start(): "
				     "dma start failed, channel %d",
				     chan->index);
		multidma_stop_links(chan);
		goto out;
	}

	chdata = dma_chan_get_data(chan);
	next.elem.size = chdata->src_period_bytes;

	if (ret) {
		trace_multidma_error("multidma_start(): "
				     "preload failed, channel %d",
				     chan->index);
		multidma_stop_links(chan);
		goto out;
	}

out:
	spin_unlock_irq(chan->dma->lock, lock_flags);

	if (!ret)
		chan->cb(chan->cb_data, DMA_CB_TYPE_COPY, &next);

	return ret;
}

static int multidma_stop(struct dma_chan_data *chan)
{
	uint32_t lock_flags;
	int ret;

	trace_multidma("multidma_stop(): channel %d", chan->index);

	spin_lock_irq(chan->dma->lock, lock_flags);

	chan->status = COMP_STATE_PREPARE;
	ret = multidma_stop_links(chan);

	if (ret)
		trace_multidma_error("multidma_stop(): error, channel %d",
				     chan->index);

	spin_unlock_irq(chan->dma->lock, lock_flags);

	return ret;
}

static int multidma_pause(struct dma_chan_data *chan)
{
	uint32_t lock_flags;
	int ret = 0;

	trace_multidma("multidma_pause(): channel %d", chan->index);

	spin_lock_irq(chan->dma->lock, lock_flags);

	if (chan->status == COMP_STATE_ACTIVE) {
		ret = multidma_pause_links(chan);
		chan->status = COMP_STATE_PAUSED;
	}

	if (ret)
		trace_multidma_error("multidma_pause(): error, channel %d",
				     chan->index);

	spin_unlock_irq(chan->dma->lock, lock_flags);

	return ret;
}

static int multidma_release(struct dma_chan_data *chan)
{
	uint32_t lock_flags;
	int ret = 0;

	trace_multidma("multidma_release(): channel %d", chan->index);

	spin_lock_irq(chan->dma->lock, lock_flags);

	if (chan->status == COMP_STATE_PAUSED) {
		ret = multidma_release_links(chan);
		chan->status = COMP_STATE_ACTIVE;
	}

	if (ret)
		trace_multidma_error("multidma_release(): error, channel %d",
				     chan->index);

	spin_unlock_irq(chan->dma->lock, lock_flags);

	return ret;
}

static int multidma_status(struct dma_chan_data *chan,
			   struct dma_chan_status *status, uint8_t direction)
{
	struct multidma_chan_data *chdata = dma_chan_get_data(chan);
	uint32_t lock_flags;
	int i;
	int ret = 0;

	spin_lock_irq(chan->dma->lock, lock_flags);

	/* Take the status from the first child link */
	for (i = 0; i < chdata->num_links; ++i) {
		if (chdata->links[i].channel >= 0) {
			ret = dma_status(chan, status, direction);
			break;
		}
	}

	spin_unlock_irq(chan->dma->lock, lock_flags);

	if (ret)
		trace_multidma_error("multidma_status(): error, channel %d",
				     chan->index);

	return ret;
}

static int multidma_get_link_fifo(struct dma_sg_config *config, uint32_t *fifo,
				  uint32_t link)
{
	int i;
	struct dma_p_info *dma_info = &config->multi.dma_info;

	for (i = 0; i < dma_info->num_links; ++i) {
		if (dma_info->elems[i].link_id == link) {
			*fifo = dma_info->elems[i].fifo;
			return 0;
		}
	}

	return -ENODEV;
}

static int multidma_get_link_handshake(struct dma_sg_config *config,
				       uint32_t *handshake, uint32_t link)
{
	int i;
	struct dma_p_info *dma_info = &config->multi.dma_info;

	for (i = 0; i < dma_info->num_links; ++i) {
		if (dma_info->elems[i].link_id == link) {
			*handshake = dma_info->elems[i].handshake;
			return 0;
		}
	}

	return -ENODEV;
}

static int multidma_verify_descriptors(struct dma_chan_data *chan,
				       struct dma_sg_config *config)
{
	struct multidma_chan_data *chdata = dma_chan_get_data(chan);
	uint32_t buf_size = 0;
	struct dma_sg_elem *elem;
	int i;

	/* Period has to contain an equal amount of data for all links */
	if (chdata->src_period_bytes % chdata->num_links) {
		trace_multidma_error("multidma_verify_descriptors(): channel %d"
				     " period not equally divisible, "
				     "src period size: %d num links: %d",
				     chan->index, chdata->src_period_bytes,
				     chdata->num_links);
		return -EINVAL;
	}

	for (i = 0; i < config->elem_array.count; ++i) {
		elem = &config->elem_array.elems[i];

		if ((char *)chdata->src + buf_size != (char *)elem->src) {
			trace_multidma_error("multidma_verify_descriptors(): "
					     "channel %d dma descriptors not "
					     "continuous", chan->index);

			return -EINVAL;
		}

		if (elem->size != chdata->src_period_bytes) {
			trace_multidma_error("multidma_verify_descriptors(): "
					     "channel %d dma descriptors not "
					     "even", chan->index);

			return -EINVAL;
		}

		buf_size += elem->size;
	}

	return buf_size;
}

static int multidma_alloc_buffer(struct dma_chan_data *chan,
				 struct dma_sg_config *config)
{
	struct multidma_chan_data *chdata = dma_chan_get_data(chan);
	struct multidma_chan_link *link;
	int buf_size = 0;
	uint32_t src_period_size;
	uint32_t targ_period_size;
	uint32_t fifo;
	int i;
	int ret = 0;

	chdata->src = (void *)config->elem_array.elems[0].src;
	chdata->src_r_ptr = chdata->src;
	src_period_size = config->elem_array.elems[0].size;
	targ_period_size = src_period_size / chdata->num_links;

	if (src_period_size % config->multi.stream_map->num_ch_map) {
		trace_multidma_error("multidma_alloc_buffer(): channel %d "
				     "period size not divisible by "
				     "no. of channels: %d", chan->index,
				     config->multi.stream_map->num_ch_map);
		return -EINVAL;
	}

	buf_size = multidma_verify_descriptors(chan, config);
	if (buf_size < 0)
		return buf_size; /* buf_size is error code */

	chdata->src_bytes = buf_size;

	chdata->buf = rballoc_align(RZONE_BUFFER,
				    SOF_MEM_CAPS_RAM | SOF_MEM_CAPS_DMA,
				    buf_size, PLATFORM_DCACHE_ALIGN);

	if (!chdata->buf) {
		trace_multidma_error("multidma_alloc_buffer(): channel %d "
				     "failed to allocate buffer, size %d",
				     chan->index, buf_size);
		return -ENOMEM;
	}

	trace_multidma("multidma_alloc_buffer(): buf 0x%X - 0x%X",
		       (uint32_t)chdata->buf,
		       (uint32_t)chdata->buf + buf_size);

	trace_multidma("multidma_alloc_buffer(): "
		       "src period bytes %d link period bytes %d",
		       src_period_size, targ_period_size);

	for (i = 0; i < chdata->num_links; ++i) {
		link = &chdata->links[i];

		ret = multidma_get_link_fifo(config, &fifo, link->link);
		if (ret)
			goto fail;

		link->buf = (char *)chdata->buf +
			(targ_period_size * config->elem_array.count * i);
		link->buf_w_ptr = link->buf;

		trace_multidma("multidma_alloc_buffer(): "
			       "link %d buf 0x%X fifo 0x%X", i,
			       (uint32_t)link->buf, fifo);

		ret = dma_sg_alloc(&link->elem_array, RZONE_RUNTIME,
				   config->direction,
				   config->elem_array.count,
				   targ_period_size,
				   (uintptr_t)link->buf, fifo);

		if (ret)
			goto fail;
	}

	return 0;

fail:
	rfree(chdata->buf);
	return ret;
}

static void multidma_set_link_channel(struct dma_sg_config *config,
				      struct multidma_chan_link *link,
				      uint32_t chmask)
{
	struct sof_ipc_stream_map *smap = config->multi.stream_map;
	struct sof_ipc_channel_map *chmap;
	int i;

	for (i = 0; i < smap->num_ch_map; ++i) {
		chmap = get_channel_map(smap, i);

		if (chmap->ext_id == link->link && chmap->ch_mask & chmask) {
			link->roffsets[link->num_txforms++] =
				chmap->ch_index * config->multi.ch_bytes;
		}
	}
}

static void multidma_set_link_offsets(struct dma_sg_config *config,
				      struct multidma_chan_link *link)
{
	int targ_ch;

	for (targ_ch = 0; targ_ch < 32; ++targ_ch)
		multidma_set_link_channel(config, link, BIT(targ_ch));
}

static int multidma_set_link_config(struct dma_chan_data *chan,
				    struct dma_sg_config *config)
{
	struct multidma_chan_data *chdata = dma_chan_get_data(chan);
	struct multidma_chan_link *link;
	struct dma_sg_config link_config;
	uint32_t handshake;
	int ret = 0;
	int i;

	memcpy_s(&link_config, sizeof(link_config), config, sizeof(*config));

	for (i = 0; i < chdata->num_links; ++i) {
		link = &chdata->links[i];

		memcpy_s(&link_config.elem_array,
			 sizeof(link_config.elem_array), &link->elem_array,
			 sizeof(link->elem_array));

		ret = multidma_get_link_handshake(config, &handshake,
						  link->link);

		if (ret) {
			trace_multidma_error("multidma_set_link_config(): "
					     "no link %d dma info"
					     "channel %d", link->link,
					     chan->index);

			break;
		}

		trace_multidma("multidma_set_link_config(): "
			       "link %d handshake %d", link->link, handshake);

		if (config->direction == DMA_DIR_MEM_TO_DEV) {
			link_config.dest_dev = handshake;
			link_config.src_dev = 0;
		} else {
			link_config.dest_dev = 0;
			link_config.src_dev = handshake;
		}

		ret = dma_set_config(link->channel, &link_config);

		if (ret) {
			trace_multidma_error("multidma_set_link_config(): "
					     "failed to set config for link %d "
					     "channel %d", link->link,
					     chan->index);

			break;
		}
	}

	return ret;
}

static int multidma_set_config(struct dma_chan_data *chan,
			       struct dma_sg_config *config)
{
	struct multidma_chan_data *chdata = dma_chan_get_data(chan);
	uint32_t lock_flags;
	int i;
	int ret = 0;

	spin_lock_irq(chan->dma->lock, lock_flags);

	trace_multidma("multidma_set_config(): channel %d", chan->index);

	if (chan->status == COMP_STATE_ACTIVE)
		return 0;

	if (!config->multi.stream_map->num_ch_map) {
		trace_multidma_error("multidma_set_config() error: "
				     "stream_map num_ch_map is 0, channel %d",
				     chan->index);

		return -EINVAL;
	}

	chdata->dma = dma_get(config->direction, config->multi.dma_caps,
			      config->multi.dma_dev, DMA_ACCESS_SHARED);

	chdata->src_period_bytes = config->elem_array.elems[0].size;
	chdata->ch_bytes = config->multi.ch_bytes;

	trace_multidma("multidma_set_config(): src period bytes %d ch bytes %d",
		       chdata->src_period_bytes, chdata->ch_bytes);

	if (!chdata->dma) {
		trace_multidma_error("multidma_set_config(): failed to "
				     "allocate child dma for channel %d",
				     chan->index);
		goto out;
	}

	ret = multidma_init_links(chan, config);
	if (ret) {
		trace_multidma_error("multidma_set_config(): failed to "
				     "init links for channel %d",
				     chan->index);
		multidma_free_links(chan);
		goto out;
	}

	for (i = 0; i < chdata->num_links; ++i)
		multidma_set_link_offsets(config, &chdata->links[i]);

	ret = multidma_alloc_buffer(chan, config);
	if (ret) {
		multidma_free_links(chan);
		goto out;
	}

	ret = multidma_set_link_config(chan, config);
	if (ret) {
		multidma_free_links(chan);
		goto out;
	}

	trace_multidma("multidma_set_config(): %d links configured",
		       chdata->num_links);

	chan->direction = config->direction;
	chan->status = COMP_STATE_PREPARE;

out:
	spin_unlock_irq(chan->dma->lock, lock_flags);

	return ret;
}

static int multidma_set_cb(struct dma_chan_data *chan, int cb_type,
			   void (*cb)(void *data, uint32_t type,
				      struct dma_cb_data *next),
			   void *cb_data)
{
	uint32_t lock_flags;

	spin_lock_irq(chan->dma->lock, lock_flags);

	trace_multidma("multidma_set_cb(): type %d channel %d", cb_type,
		       chan->index);

	chan->cb = cb;
	chan->cb_type = cb_type;
	chan->cb_data = cb_data;

	spin_unlock_irq(chan->dma->lock, lock_flags);
	return 0;
}

static int multidma_pm_context_restore(struct dma *dma)
{
	return 0;
}

static int multidma_pm_context_store(struct dma *dma)
{
	return 0;
}

static int multidma_probe(struct dma *dma)
{
	struct dma_chan_data *chan;
	struct multidma_chan_data *chdata;
	struct multidma_chan_link *link;
	int i;
	int j;
	uint32_t allocated = 0;

	trace_multidma("multidma_probe()");

	if (dma->chan)
		return -EEXIST;

	allocated += sizeof(*dma->chan) * dma->plat_data.channels;

	dma->chan = rzalloc(RZONE_SYS_RUNTIME | RZONE_FLAG_UNCACHED,
			    SOF_MEM_CAPS_RAM, sizeof(*dma->chan) *
			    dma->plat_data.channels);

	if (!dma->chan) {
		trace_multidma_error("multidma_probe(): "
				     "failed to allocate %d channels",
				     dma->plat_data.channels);
		return -ENOMEM;
	}

	for (i = 0; i < dma->plat_data.channels; ++i) {
		chan = &dma->chan[i];
		chan->dma = dma;
		chan->index = i;
		chan->status = COMP_STATE_INIT;

		chdata = rzalloc(RZONE_SYS_RUNTIME | RZONE_FLAG_UNCACHED,
				 SOF_MEM_CAPS_RAM, sizeof(*chdata));

		allocated += sizeof(*chdata);

		dma_chan_set_data(dma->chan, chdata);

		for (j = 0; j < MULTIDMA_MAX_LINKS; ++j) {
			link = &chdata->links[i];
			link->link = -1;
			link->channel = NULL;
		}
	}

	trace_multidma("multidma_probe() num ch %u chdata %u ALLOC %u",
		       dma->plat_data.channels, sizeof(*chdata), allocated);

	atomic_init(&dma->num_channels_busy, 0);

	return 0;
}

static int multidma_remove(struct dma *dma)
{
	struct multidma_chan_data *chdata;
	int i;

	trace_multidma("multidma_remove()");

	for (i = 0; i < dma->plat_data.channels; ++i) {
		chdata = dma_chan_get_data(&dma->chan[i]);

		if (chdata)
			rfree(chdata);

		dma_chan_set_data(&dma->chan[i], NULL);
	}

	rfree(dma->chan);

	return 0;
}

static int multidma_data_size(struct dma_chan_data *chan, uint32_t *avail,
			      uint32_t *free)
{
	struct multidma_chan_data *chdata = dma_chan_get_data(chan);
	uint32_t lock_flags;
	int ret;
	int i;
	uint32_t cur_avail;
	uint32_t cur_free;

	spin_lock_irq(chan->dma->lock, lock_flags);

	if (chan->direction == DMA_DIR_DEV_TO_MEM)
		*avail = -1;
	else
		*free = -1;

	for (i = 0; i < chdata->num_links; ++i) {
		ret = dma_get_data_size(chdata->links[i].channel, &cur_avail,
					&cur_free);

		if (ret) {
			trace_multidma_error("multidma_data_size(): "
					     "failed for link %d channel %d",
					     chdata->links[i].link,
					     chan->index);
			return -EINVAL;
		}

		if (chan->direction == DMA_DIR_DEV_TO_MEM) {
			*avail = MIN(*avail, cur_avail * chdata->num_links);

			tracev_multidma("multidma_data_size(): "
					"link %d avail %d",
					chdata->links[i].link, cur_avail);
		} else {
			*free = MIN(*free, cur_free * chdata->num_links);

			tracev_multidma("multidma_data_size(): "
					"link %d free %d",
					chdata->links[i].link, cur_free);
		}
	}

	spin_unlock_irq(chan->dma->lock, lock_flags);

	return 0;
}

static int multidma_get_attribute(struct dma *dma, uint32_t type,
				  uint32_t *value)
{
	int ret = 0;

	switch (type) {
	case DMA_ATTR_BUFFER_ALIGNMENT:
		*value = MULTIDMA_BUF_ALIGN;
		break;
	case DMA_ATTR_COPY_ALIGNMENT:
		*value = MULTIDMA_CPY_ALIGN;
		break;
	case DMA_ATTR_BUFFER_ADDRESS_ALIGNMENT:
		*value = PLATFORM_DCACHE_ALIGN;
		break;
	case DMA_ATTR_BUFFER_PERIOD_COUNT:
		*value = MULTIDMA_PERIOD_COUNT;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

const struct dma_ops multidma_ops = {
	.channel_get		= multidma_channel_get,
	.channel_put		= multidma_channel_put,
	.start			= multidma_start,
	.stop			= multidma_stop,
	.copy			= multidma_copy,
	.pause			= multidma_pause,
	.release		= multidma_release,
	.status			= multidma_status,
	.set_config		= multidma_set_config,
	.set_cb			= multidma_set_cb,
	.pm_context_restore	= multidma_pm_context_restore,
	.pm_context_store	= multidma_pm_context_store,
	.probe			= multidma_probe,
	.remove			= multidma_remove,
	.get_data_size		= multidma_data_size,
	.get_attribute		= multidma_get_attribute,
};
