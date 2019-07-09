// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//         Keyon Jie <yang.jie@linux.intel.com>

#include <errno.h>
#include <stdbool.h>
#include <sof/stream.h>
#include <sof/ssp.h>
#include <sof/alloc.h>
#include <sof/interrupt.h>

/* tracing */
#define trace_ssp(__e, ...) \
	trace_event(TRACE_CLASS_SSP, __e, ##__VA_ARGS__)
#define trace_ssp_error(__e, ...) \
	trace_error(TRACE_CLASS_SSP, __e, ##__VA_ARGS__)
#define tracev_ssp(__e, ...) \
	tracev_event(TRACE_CLASS_SSP, __e, ##__VA_ARGS__)

/* save SSP context prior to entering D3 */
static int ssp_context_store(struct dai *dai)
{
	struct ssp_pdata *ssp = dai_get_drvdata(dai);

	ssp->sscr0 = ssp_read(dai, SSCR0);
	ssp->sscr1 = ssp_read(dai, SSCR1);

	/* FIXME: need to store sscr2,3,4,5 */
	ssp->psp = ssp_read(dai, SSPSP);

	return 0;
}

/* restore SSP context after leaving D3 */
static int ssp_context_restore(struct dai *dai)
{
	struct ssp_pdata *ssp = dai_get_drvdata(dai);

	ssp_write(dai, SSCR0, ssp->sscr0);
	ssp_write(dai, SSCR1, ssp->sscr1);
	/* FIXME: need to restore sscr2,3,4,5 */
	ssp_write(dai, SSPSP, ssp->psp);

	return 0;
}

/* Digital Audio interface formatting */
static int ssp_set_config(struct dai *dai,
			  struct sof_ipc_dai_config *config)
{
	struct ssp_pdata *ssp = dai_get_drvdata(dai);
	uint32_t sscr0;
	uint32_t sscr1;
	uint32_t sscr2;
	uint32_t sspsp;
	uint32_t sspsp2;
	uint32_t mdiv;
	uint32_t bdiv;
	uint32_t data_size;
	uint32_t start_delay;
	uint32_t frame_end_padding;
	uint32_t slot_end_padding;
	uint32_t frame_len = 0;
	uint32_t bdiv_min;
	uint32_t format;
	bool inverted_frame = false;
	int ret = 0;

	spin_lock(&dai->lock);

	/* is playback/capture already running */
	if (ssp->state[DAI_DIR_PLAYBACK] == COMP_STATE_ACTIVE ||
	    ssp->state[DAI_DIR_CAPTURE] == COMP_STATE_ACTIVE) {
		trace_ssp_error("ssp_set_config() error: "
				"playback/capture already running");
		ret = -EINVAL;
		goto out;
	}

	trace_ssp("ssp_set_config()");

	/* disable clock */
	shim_update_bits(SHIM_CLKCTL, SHIM_CLKCTL_EN_SSP(dai->index), 0);

	/* enable MCLK */
	shim_update_bits(SHIM_CLKCTL, SHIM_CLKCTL_SMOS(0x3),
			 SHIM_CLKCTL_SMOS(0x3));

	/* reset SSP settings */
	/* sscr0 dynamic settings are DSS, EDSS, SCR, FRDC, ECS */
	sscr0 = SSCR0_MOD | SSCR0_PSP;

	/* sscr1 dynamic settings are TFT, RFT, SFRMDIR, SCLKDIR, SCFR */
	sscr1 = SSCR1_TTE | SSCR1_TTELP;

	/* enable Transmit underrun mode 1 */
	sscr2 = SSCR2_TURM1;

	/* sspsp dynamic settings are SCMODE, SFRMP, DMYSTRT, SFRMWDTH */
	sspsp = 0x0;

	/* sspsp2 no dynamic setting */
	sspsp2 = 0x0;

	ssp->config = *config;
	ssp->params = config->ssp;

	switch (config->format & SOF_DAI_FMT_MASTER_MASK) {
	case SOF_DAI_FMT_CBM_CFM:
		sscr1 |= SSCR1_SCLKDIR | SSCR1_SFRMDIR;
#ifdef ENABLE_SSRCR1_SCFR
		sscr1 |= SSCR1_SCFR;
#endif
		break;
	case SOF_DAI_FMT_CBS_CFS:
		break;
	case SOF_DAI_FMT_CBM_CFS:
		sscr1 |= SSCR1_SCLKDIR;
#ifdef ENABLE_SSRCR1_SCFR
		sscr1 |= SSCR1_SCFR;
#endif
		break;
	case SOF_DAI_FMT_CBS_CFM:
		sscr1 |= SSCR1_SFRMDIR;
		break;
	default:
		trace_ssp_error("ssp_set_config() error: "
				"format & MASTER_MASK EINVAL");
		ret = -EINVAL;
		goto out;
	}

	/* clock signal polarity */
	switch (config->format & SOF_DAI_FMT_INV_MASK) {
	case SOF_DAI_FMT_NB_NF:
		break;
	case SOF_DAI_FMT_NB_IF:
		inverted_frame = true; /* handled later with format */
		break;
	case SOF_DAI_FMT_IB_IF:
		sspsp |= SSPSP_SCMODE(2);
		inverted_frame = true; /* handled later with format */
		break;
	case SOF_DAI_FMT_IB_NF:
		sspsp |= SSPSP_SCMODE(2);
		break;
	default:
		trace_ssp_error("ssp_set_config() error: "
				"format & INV_MASK EINVAL");
		ret = -EINVAL;
		goto out;
	}

	/* Additional hardware settings */

	/* Receiver Time-out Interrupt Disabled/Enabled */
	sscr1 |= (ssp->params.quirks & SOF_DAI_INTEL_SSP_QUIRK_TINTE) ?
		SSCR1_TINTE : 0;

	/* Peripheral Trailing Byte Interrupts Disable/Enable */
	sscr1 |= (ssp->params.quirks & SOF_DAI_INTEL_SSP_QUIRK_PINTE) ?
		SSCR1_PINTE : 0;

	/* Enable/disable internal loopback. Output of transmit serial
	 * shifter connected to input of receive serial shifter, internally.
	 */
	sscr1 |= (ssp->params.quirks & SOF_DAI_INTEL_SSP_QUIRK_LBM) ?
		SSCR1_LBM : 0;

	/* Transmit data are driven at the same/opposite clock edge specified
	 * in SSPSP.SCMODE[1:0]
	 */
	sscr2 |= (ssp->params.quirks & SOF_DAI_INTEL_SSP_QUIRK_SMTATF) ?
		SSCR2_SMTATF : 0;

	/* Receive data are sampled at the same/opposite clock edge specified
	 * in SSPSP.SCMODE[1:0]
	 */
	sscr2 |= (ssp->params.quirks & SOF_DAI_INTEL_SSP_QUIRK_MMRATF) ?
		SSCR2_MMRATF : 0;

	/* Enable/disable the fix for PSP slave mode TXD wait for frame
	 * de-assertion before starting the second channel
	 */
	sscr2 |= (ssp->params.quirks & SOF_DAI_INTEL_SSP_QUIRK_PSPSTWFDFD) ?
		SSCR2_PSPSTWFDFD : 0;

	/* Enable/disable the fix for PSP master mode FSRT with dummy stop &
	 * frame end padding capability
	 */
	sscr2 |= (ssp->params.quirks & SOF_DAI_INTEL_SSP_QUIRK_PSPSRWFDFD) ?
		SSCR2_PSPSRWFDFD : 0;

	/* BCLK is generated from MCLK - must be divisable */
	if (config->ssp.mclk_rate % config->ssp.bclk_rate) {
		trace_ssp_error("ssp_set_config() error: "
				"MCLK is not divisable");
		ret = -EINVAL;
		goto out;
	}

	/* divisor must be within SCR range */
	mdiv = (config->ssp.mclk_rate / config->ssp.bclk_rate) - 1;
	if (mdiv > (SSCR0_SCR_MASK >> 8)) {
		trace_ssp_error("ssp_set_config() error: "
				"divisor is not within SCR range");
		ret = -EINVAL;
		goto out;
	}

	/* set the SCR divisor */
	sscr0 |= SSCR0_SCR(mdiv);

	/* calc frame width based on BCLK and rate - must be divisable */
	if (config->ssp.bclk_rate % config->ssp.fsync_rate) {
		trace_ssp_error("ssp_set_config() error: "
				"BLCK is not divisable");
		ret = -EINVAL;
		goto out;
	}

	/* must be enouch BCLKs for data */
	bdiv = config->ssp.bclk_rate / config->ssp.fsync_rate;
	if (bdiv < config->ssp.tdm_slot_width *
	    config->ssp.tdm_slots) {
		trace_ssp_error("ssp_set_config() error: not enough BCLKs");
		ret = -EINVAL;
		goto out;
	}

	/* tdm_slot_width must be <= 38 for SSP */
	if (config->ssp.tdm_slot_width > 38) {
		trace_ssp_error("ssp_set_config() error: tdm_slot_width > 38");
		ret = -EINVAL;
		goto out;
	}

	bdiv_min = config->ssp.tdm_slots * config->ssp.sample_valid_bits;
	if (bdiv < bdiv_min) {
		trace_ssp_error("ssp_set_config() error: bdiv < bdiv_min");
		ret = -EINVAL;
		goto out;
	}

	frame_end_padding = bdiv - bdiv_min;
	if (frame_end_padding > SSPSP2_FEP_MASK) {
		trace_ssp_error("ssp_set_config() error: "
				"frame_end_padding > SSPSP2_FEP_MASK");
		ret = -EINVAL;
		goto out;
	}

	/* format */
	format = config->format & SOF_DAI_FMT_FORMAT_MASK;
	switch (format) {
	case SOF_DAI_FMT_I2S:
	case SOF_DAI_FMT_LEFT_J:

		if (format == SOF_DAI_FMT_I2S) {
			start_delay = 1;

		/*
		 * handle frame polarity, I2S default is falling/active low,
		 * non-inverted(inverted_frame=0) -- active low(SFRMP=0),
		 * inverted(inverted_frame=1) -- rising/active high(SFRMP=1),
		 * so, we should set SFRMP to inverted_frame.
		 */
			sspsp |= SSPSP_SFRMP(inverted_frame);
			sspsp |= SSPSP_FSRT;

		} else {
			start_delay = 0;

		/*
		 * handle frame polarity, LEFT_J default is rising/active high,
		 * non-inverted(inverted_frame=0) -- active high(SFRMP=1),
		 * inverted(inverted_frame=1) -- falling/active low(SFRMP=0),
		 * so, we should set SFRMP to !inverted_frame.
		 */
			sspsp |= SSPSP_SFRMP(!inverted_frame);
		}

		sscr0 |= SSCR0_FRDC(config->ssp.tdm_slots);

		if (bdiv % 2) {
			trace_ssp_error("ssp_set_config() error: "
					"bdiv is not divisible by 2");
			ret = -EINVAL;
			goto out;
		}

		/* set asserted frame length to half frame length */
		frame_len = bdiv / 2;

		/*
		 *  for I2S/LEFT_J, the padding has to happen at the end
		 * of each slot
		 */
		if (frame_end_padding % 2) {
			trace_ssp_error("ssp_set_config() error: "
					"frame_end_padding "
					"is not divisible by 2");
			ret = -EINVAL;
			goto out;
		}

		slot_end_padding = frame_end_padding / 2;

		if (slot_end_padding > 15) {
			/* can't handle padding over 15 bits */
			trace_ssp_error("ssp_set_config() error: "
					"slot_end_padding over 15 bits");
			ret = -EINVAL;
			goto out;
		}

		sspsp |= SSPSP_DMYSTOP(slot_end_padding & SSPSP_DMYSTOP_MASK);
		slot_end_padding >>= SSPSP_DMYSTOP_BITS;
		sspsp |= SSPSP_EDMYSTOP(slot_end_padding & SSPSP_EDMYSTOP_MASK);

		break;
	case SOF_DAI_FMT_DSP_A:

		start_delay = 1;

		sscr0 |= SSCR0_FRDC(config->ssp.tdm_slots);

		/* set asserted frame length */
		frame_len = 1;

		/* handle frame polarity, DSP_A default is rising/active high */
		sspsp |= SSPSP_SFRMP(!inverted_frame);
		sspsp2 |= (frame_end_padding & SSPSP2_FEP_MASK);

		break;
	case SOF_DAI_FMT_DSP_B:

		start_delay = 0;

		sscr0 |= SSCR0_FRDC(config->ssp.tdm_slots);

		/* set asserted frame length */
		frame_len = 1;

		/* handle frame polarity, DSP_B default is rising/active high */
		sspsp |= SSPSP_SFRMP(!inverted_frame);
		sspsp2 |= (frame_end_padding & SSPSP2_FEP_MASK);

		break;
	default:
		trace_ssp_error("ssp_set_config() error: invalid format");
		ret = -EINVAL;
		goto out;
	}

	if (start_delay)
		sspsp |= SSPSP_FSRT;

	sspsp |= SSPSP_SFRMWDTH(frame_len);

	data_size = config->ssp.sample_valid_bits;

	if (data_size > 16)
		sscr0 |= (SSCR0_EDSS | SSCR0_DSIZE(data_size - 16));
	else
		sscr0 |= SSCR0_DSIZE(data_size);

	sscr1 |= SSCR1_TFT(0x8) | SSCR1_RFT(0x8);

	ssp_write(dai, SSCR0, sscr0);
	ssp_write(dai, SSCR1, sscr1);
	ssp_write(dai, SSCR2, sscr2);
	ssp_write(dai, SSPSP, sspsp);
	ssp_write(dai, SSTSA, config->ssp.tx_slots);
	ssp_write(dai, SSRSA, config->ssp.rx_slots);
	ssp_write(dai, SSPSP2, sspsp2);

	ssp->state[DAI_DIR_PLAYBACK] = COMP_STATE_PREPARE;
	ssp->state[DAI_DIR_CAPTURE] = COMP_STATE_PREPARE;

	/* enable clock */
	shim_update_bits(SHIM_CLKCTL, SHIM_CLKCTL_EN_SSP(dai->index),
			 SHIM_CLKCTL_EN_SSP(dai->index));

	/* enable free running clock */
	ssp_update_bits(dai, SSCR0, SSCR0_SSE, SSCR0_SSE);
	ssp_update_bits(dai, SSCR0, SSCR0_SSE, 0);

	trace_ssp("ssp_set_config(), done");

out:
	spin_unlock(&dai->lock);

	return ret;
}

/* start the SSP for either playback or capture */
static void ssp_start(struct dai *dai, int direction)
{
	struct ssp_pdata *ssp = dai_get_drvdata(dai);

	spin_lock(&dai->lock);

	trace_ssp("ssp_start()");

	/* enable DMA */
	if (direction == DAI_DIR_PLAYBACK) {
		ssp_update_bits(dai, SSCR1, SSCR1_TSRE | SSCR1_EBCEI,
				SSCR1_TSRE | SSCR1_EBCEI);
		ssp_update_bits(dai, SSCR0, SSCR0_SSE, SSCR0_SSE);
		ssp_update_bits(dai, SSCR0, SSCR0_TIM, 0);
		ssp_update_bits(dai, SSTSA, SSTSA_TSEN, SSTSA_TSEN);
	} else {
		ssp_update_bits(dai, SSCR1, SSCR1_RSRE | SSCR1_EBCEI,
				SSCR1_RSRE | SSCR1_EBCEI);
		ssp_update_bits(dai, SSCR0, SSCR0_SSE, SSCR0_SSE);
		ssp_update_bits(dai, SSCR0, SSCR0_RIM, 0);
		ssp_update_bits(dai, SSRSA, SSRSA_RSEN, SSRSA_RSEN);
	}

	/* enable port */
	ssp->state[direction] = COMP_STATE_ACTIVE;

	spin_unlock(&dai->lock);
}

/* stop the SSP for either playback or capture */
static void ssp_stop(struct dai *dai, int direction)
{
	struct ssp_pdata *ssp = dai_get_drvdata(dai);

	spin_lock(&dai->lock);

	/* stop Rx if neeed */
	if (direction == DAI_DIR_CAPTURE &&
	    ssp->state[SOF_IPC_STREAM_CAPTURE] == COMP_STATE_ACTIVE) {
		ssp_update_bits(dai, SSCR1, SSCR1_RSRE, 0);
		ssp_update_bits(dai, SSCR0, SSCR0_RIM, SSCR0_RIM);
		ssp->state[SOF_IPC_STREAM_CAPTURE] = COMP_STATE_PAUSED;
		trace_ssp("ssp_stop(), RX stop");
	}

	/* stop Tx if needed */
	if (direction == DAI_DIR_PLAYBACK &&
	    ssp->state[SOF_IPC_STREAM_PLAYBACK] == COMP_STATE_ACTIVE) {
		ssp_update_bits(dai, SSCR1, SSCR1_TSRE, 0);
		ssp_update_bits(dai, SSCR0, SSCR0_TIM, SSCR0_TIM);
		ssp->state[SOF_IPC_STREAM_PLAYBACK] = COMP_STATE_PAUSED;
		trace_ssp("ssp_stop(), TX stop");
	}

	/* disable SSP port if no users */
	if (ssp->state[SOF_IPC_STREAM_CAPTURE] != COMP_STATE_ACTIVE &&
	    ssp->state[SOF_IPC_STREAM_PLAYBACK] != COMP_STATE_ACTIVE) {
		ssp_update_bits(dai, SSCR0, SSCR0_SSE, 0);
		ssp->state[SOF_IPC_STREAM_CAPTURE] = COMP_STATE_PREPARE;
		ssp->state[SOF_IPC_STREAM_PLAYBACK] = COMP_STATE_PREPARE;
		trace_ssp("ssp_stop(), SSP port disabled");
	}

	spin_unlock(&dai->lock);
}

static int ssp_trigger(struct dai *dai, int cmd, int direction)
{
	struct ssp_pdata *ssp = dai_get_drvdata(dai);

	trace_ssp("ssp_trigger()");

	switch (cmd) {
	case COMP_TRIGGER_START:
		if (ssp->state[direction] == COMP_STATE_PREPARE ||
		    ssp->state[direction] == COMP_STATE_PAUSED)
			ssp_start(dai, direction);
		break;
	case COMP_TRIGGER_RELEASE:
		if (ssp->state[direction] == COMP_STATE_PAUSED ||
		    ssp->state[direction] == COMP_STATE_PREPARE)
			ssp_start(dai, direction);
		break;
	case COMP_TRIGGER_STOP:
	case COMP_TRIGGER_PAUSE:
		ssp_stop(dai, direction);
		break;
	case COMP_TRIGGER_RESUME:
		ssp_context_restore(dai);
		break;
	case COMP_TRIGGER_SUSPEND:
		ssp_context_store(dai);
		break;
	default:
		break;
	}

	return 0;
}

static int ssp_probe(struct dai *dai)
{
	struct ssp_pdata *ssp;

	/* allocate private data */
	ssp = rzalloc(RZONE_SYS | RZONE_FLAG_UNCACHED, SOF_MEM_CAPS_RAM,
		      sizeof(*ssp));
	dai_set_drvdata(dai, ssp);

	spinlock_init(&dai->lock);

	ssp->state[DAI_DIR_PLAYBACK] = COMP_STATE_READY;
	ssp->state[DAI_DIR_CAPTURE] = COMP_STATE_READY;

	return 0;
}

static int ssp_get_handshake(struct dai *dai, int direction, int stream_id)
{
	return dai->plat_data.fifo[direction].handshake;
}

static int ssp_get_fifo(struct dai *dai, int direction, int stream_id)
{
	return dai->plat_data.fifo[direction].offset;
}

const struct dai_driver ssp_driver = {
	.type = SOF_DAI_INTEL_SSP,
	.dma_caps = DMA_CAP_GP_LP | DMA_CAP_GP_HP,
	.dma_dev = DMA_DEV_SSP,
	.ops = {
		.trigger		= ssp_trigger,
		.set_config		= ssp_set_config,
		.pm_context_store	= ssp_context_store,
		.pm_context_restore	= ssp_context_restore,
		.get_handshake		= ssp_get_handshake,
		.get_fifo		= ssp_get_fifo,
		.probe			= ssp_probe,
	},
};
