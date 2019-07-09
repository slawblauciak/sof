// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2016 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//         Keyon Jie <yang.jie@linux.intel.com>
//         Rander Wang <rander.wang@linux.intel.com>

#include <errno.h>
#include <stdbool.h>
#include <sof/stream.h>
#include <sof/ssp.h>
#include <sof/alloc.h>
#include <sof/interrupt.h>
#include <sof/pm_runtime.h>
#include <sof/math/numbers.h>
#include <config.h>

/* tracing */
#define trace_ssp(__e, ...) \
	trace_event(TRACE_CLASS_SSP, __e, ##__VA_ARGS__)
#define trace_ssp_error(__e, ...) \
	trace_error(TRACE_CLASS_SSP, __e, ##__VA_ARGS__)
#define tracev_ssp(__e, ...) \
	tracev_event(TRACE_CLASS_SSP, __e, ##__VA_ARGS__)

/* FIXME: move this to a helper and optimize */
static int hweight_32(uint32_t mask)
{
	int i;
	int count = 0;

	for (i = 0; i < 32; i++) {
		count += mask & 1;
		mask >>= 1;
	}
	return count;
}

/* empty SSP transmit FIFO */
static void ssp_empty_tx_fifo(struct dai *dai)
{
	uint32_t sssr;

	spin_lock(&dai->lock);

	sssr = ssp_read(dai, SSSR);

	/* clear interrupt */
	if (sssr & SSSR_TUR)
		ssp_write(dai, SSSR, sssr);

	spin_unlock(&dai->lock);
}

/* empty SSP receive FIFO */
static void ssp_empty_rx_fifo(struct dai *dai)
{
	uint32_t sssr;
	uint32_t entries;
	uint32_t i;

	spin_lock(&dai->lock);

	sssr = ssp_read(dai, SSSR);

	/* clear interrupt */
	if (sssr & SSSR_ROR)
		ssp_write(dai, SSSR, sssr);

	/* empty fifo */
	if (sssr & SSSR_RNE) {
		entries = (ssp_read(dai, SSCR3) & SSCR3_RFL_MASK) >> 8;
		for (i = 0; i < entries + 1; i++)
			ssp_read(dai, SSDR);
	}

	spin_unlock(&dai->lock);
}

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
	uint32_t sscr3;
	uint32_t sspsp;
	uint32_t sspsp2;
	uint32_t sstsa;
	uint32_t ssrsa;
	uint32_t ssto;
	uint32_t ssioc;
	uint32_t mdiv;
	uint32_t bdiv;
	uint32_t mdivc;
	uint32_t mdivr;
	uint32_t mdivr_val;
	uint32_t i2s_m;
	uint32_t i2s_n;
	uint32_t data_size;
	uint32_t frame_end_padding;
	uint32_t slot_end_padding;
	uint32_t frame_len = 0;
	uint32_t bdiv_min;
	uint32_t tft;
	uint32_t rft;
	uint32_t active_tx_slots = 2;
	uint32_t active_rx_slots = 2;
	uint32_t sample_width = 2;

	bool inverted_bclk = false;
	bool inverted_frame = false;
	bool cfs = false;
	bool start_delay = false;

	int i;
	int clk_index = -1;
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

	trace_ssp("ssp_set_config(), config->format = 0x%4x", config->format);

	/* reset SSP settings */
	/* sscr0 dynamic settings are DSS, EDSS, SCR, FRDC, ECS */
	/*
	 * FIXME: MOD, ACS, NCS are not set,
	 * no support for network mode for now
	 */
	sscr0 = SSCR0_PSP | SSCR0_RIM | SSCR0_TIM;

	/* sscr1 dynamic settings are SFRMDIR, SCLKDIR, SCFR */
	sscr1 = SSCR1_TTE | SSCR1_TTELP | SSCR1_TRAIL | SSCR1_RSRE | SSCR1_TSRE;

	/* sscr2 dynamic setting is LJDFD */
	sscr2 = SSCR2_SDFD | SSCR2_TURM1;

	/* sscr3 dynamic settings are TFT, RFT */
	sscr3 = 0;

	/* sspsp dynamic settings are SCMODE, SFRMP, DMYSTRT, SFRMWDTH */
	sspsp = 0;

	ssp->config = *config;
	ssp->params = config->ssp;

	/* sspsp2 no dynamic setting */
	sspsp2 = 0x0;

	/* ssioc dynamic setting is SFCR */
	ssioc = SSIOC_SCOE;

	/* i2s_m M divider setting, default 1 */
	i2s_m = 0x1;

	/* i2s_n N divider setting, default 1 */
	i2s_n = 0x1;

	/* ssto no dynamic setting */
	ssto = 0x0;

	/* sstsa dynamic setting is TTSA, default 2 slots */
	sstsa = config->ssp.tx_slots;

	/* ssrsa dynamic setting is RTSA, default 2 slots */
	ssrsa = config->ssp.rx_slots;

	switch (config->format & SOF_DAI_FMT_MASTER_MASK) {
	case SOF_DAI_FMT_CBM_CFM:
		sscr1 |= SSCR1_SCLKDIR | SSCR1_SFRMDIR;
		break;
	case SOF_DAI_FMT_CBS_CFS:
		sscr1 |= SSCR1_SCFR;
		cfs = true;
		break;
	case SOF_DAI_FMT_CBM_CFS:
		sscr1 |= SSCR1_SCLKDIR;
		/* FIXME: this mode has not been tested */

		cfs = true;
		break;
	case SOF_DAI_FMT_CBS_CFM:
		sscr1 |= SSCR1_SCFR | SSCR1_SFRMDIR;
		/* FIXME: this mode has not been tested */
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
		inverted_bclk = true; /* handled later with bclk idle */
		inverted_frame = true; /* handled later with format */
		break;
	case SOF_DAI_FMT_IB_NF:
		inverted_bclk = true; /* handled later with bclk idle */
		break;
	default:
		trace_ssp_error("ssp_set_config() error: "
				"format & INV_MASK EINVAL");
		ret = -EINVAL;
		goto out;
	}

	/* supporting bclk idle state */
	if (ssp->params.clks_control &
		SOF_DAI_INTEL_SSP_CLKCTRL_BCLK_IDLE_HIGH) {
		/* bclk idle state high */
		sspsp |= SSPSP_SCMODE((inverted_bclk ^ 0x3) & 0x3);
	} else {
		/* bclk idle state low */
		sspsp |= SSPSP_SCMODE(inverted_bclk);
	}

	sscr0 |= SSCR0_MOD | SSCR0_ACS;

	mdivc = mn_reg_read(0x0);
	mdivc |= 0x1;

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

	if (!config->ssp.mclk_rate ||
	    config->ssp.mclk_rate > ssp_freq[MAX_SSP_FREQ_INDEX].freq) {
		trace_ssp_error("ssp_set_config() error: "
				"invalid MCLK = %d Hz (valid < %d)",
				config->ssp.mclk_rate,
				ssp_freq[MAX_SSP_FREQ_INDEX].freq);
		ret = -EINVAL;
		goto out;
	}

	if (!config->ssp.bclk_rate ||
	    config->ssp.bclk_rate > config->ssp.mclk_rate) {
		trace_ssp_error("ssp_set_config() error: "
				"BCLK %d Hz = 0 or > MCLK %d Hz",
				config->ssp.bclk_rate, config->ssp.mclk_rate);
		ret = -EINVAL;
		goto out;
	}

	/* MCLK config */
	/* searching the smallest possible mclk source */
	for (i = MAX_SSP_FREQ_INDEX; i >= 0; i--) {
		if (config->ssp.mclk_rate > ssp_freq[i].freq)
			break;

		if (ssp_freq[i].freq % config->ssp.mclk_rate == 0)
			clk_index = i;
	}

	if (clk_index >= 0) {
		mdivc |= MCDSS(ssp_freq[clk_index].enc);
		mdivr_val = ssp_freq[clk_index].freq / config->ssp.mclk_rate;
	} else {
		trace_ssp_error("ssp_set_config() error: MCLK %d",
				config->ssp.mclk_rate);
		ret = -EINVAL;
		goto out;
	}

	/* BCLK config */
	/* searching the smallest possible bclk source */
	clk_index = -1;
	for (i = MAX_SSP_FREQ_INDEX; i >= 0; i--) {
		if (config->ssp.bclk_rate > ssp_freq[i].freq)
			break;

		if (ssp_freq[i].freq % config->ssp.bclk_rate == 0)
			clk_index = i;
	}

	if (clk_index >= 0) {
		mdivc |= MNDSS(ssp_freq[clk_index].enc);
		mdiv = ssp_freq[clk_index].freq / config->ssp.bclk_rate;

		/* select M/N output for bclk in case of Audio Cardinal
		 * or PLL Fixed clock.
		 */
		if (ssp_freq[clk_index].enc != CLOCK_SSP_XTAL_OSCILLATOR)
			sscr0 |= SSCR0_ECS;
	} else {
		trace_ssp_error("ssp_set_config() error: BCLK %d",
				config->ssp.bclk_rate);
		ret = -EINVAL;
		goto out;
	}


	switch (mdivr_val) {
	case 1:
		mdivr = 0x00000fff; /* bypass divider for MCLK */
		break;
	case 2:
		mdivr = 0x0; /* 1/2 */
		break;
	case 4:
		mdivr = 0x2; /* 1/4 */
		break;
	case 8:
		mdivr = 0x6; /* 1/8 */
		break;
	default:
		trace_ssp_error("ssp_set_config() error: invalid mdivr_val %d",
				mdivr_val);
		ret = -EINVAL;
		goto out;
	}

	if (config->ssp.mclk_id > 1) {
		trace_ssp_error("ssp_set_config() error: mclk ID (%d) > 1",
				config->ssp.mclk_id);
		ret = -EINVAL;
		goto out;
	}

	/* divisor must be within SCR range */
	mdiv -= 1;
	if (mdiv > (SSCR0_SCR_MASK >> 8)) {
		trace_ssp_error("ssp_set_config() error: "
				"divisor %d is not within SCR range", mdiv);
		ret = -EINVAL;
		goto out;
	}

	/* set the SCR divisor */
	sscr0 |= SSCR0_SCR(mdiv);

	/* calc frame width based on BCLK and rate - must be divisable */
	if (config->ssp.bclk_rate % config->ssp.fsync_rate) {
		trace_ssp_error("ssp_set_config() error: "
				"BCLK %d is not divisable by rate %d",
				config->ssp.bclk_rate, config->ssp.fsync_rate);
		ret = -EINVAL;
		goto out;
	}

	/* must be enough BCLKs for data */
	bdiv = config->ssp.bclk_rate / config->ssp.fsync_rate;
	if (bdiv < config->ssp.tdm_slot_width * config->ssp.tdm_slots) {
		trace_ssp_error("ssp_set_config() error: not enough BCLKs "
				"need %d", config->ssp.tdm_slot_width *
				config->ssp.tdm_slots);
		ret = -EINVAL;
		goto out;
	}

	/* tdm_slot_width must be <= 38 for SSP */
	if (config->ssp.tdm_slot_width > 38) {
		trace_ssp_error("ssp_set_config() error: tdm_slot_width %d > "
				"38", config->ssp.tdm_slot_width);
		ret = -EINVAL;
		goto out;
	}

	bdiv_min = config->ssp.tdm_slots *
		   (config->ssp.tdm_per_slot_padding_flag ?
		    config->ssp.tdm_slot_width : config->ssp.sample_valid_bits);
	if (bdiv < bdiv_min) {
		trace_ssp_error("ssp_set_config() error: bdiv(%d) < "
				"bdiv_min(%d)", bdiv < bdiv_min);
		ret = -EINVAL;
		goto out;
	}

	frame_end_padding = bdiv - bdiv_min;
	if (frame_end_padding > SSPSP2_FEP_MASK) {
		trace_ssp_error("ssp_set_config() error: frame_end_padding "
				"too big: %u", frame_end_padding);
		ret = -EINVAL;
		goto out;
	}

	/* format */
	switch (config->format & SOF_DAI_FMT_FORMAT_MASK) {
	case SOF_DAI_FMT_I2S:

		start_delay = true;

		sscr0 |= SSCR0_FRDC(config->ssp.tdm_slots);

		if (bdiv % 2) {
			trace_ssp_error("ssp_set_config() error: "
					"bdiv %d is not divisible by 2", bdiv);
			ret = -EINVAL;
			goto out;
		}

		/* set asserted frame length to half frame length */
		frame_len = bdiv / 2;

		/*
		 * handle frame polarity, I2S default is falling/active low,
		 * non-inverted(inverted_frame=0) -- active low(SFRMP=0),
		 * inverted(inverted_frame=1) -- rising/active high(SFRMP=1),
		 * so, we should set SFRMP to inverted_frame.
		 */
		sspsp |= SSPSP_SFRMP(inverted_frame);

		/*
		 *  for I2S/LEFT_J, the padding has to happen at the end
		 * of each slot
		 */
		if (frame_end_padding % 2) {
			trace_ssp_error("ssp_set_config() error: "
					"frame_end_padding %d "
					"is not divisible by 2",
					frame_end_padding);
			ret = -EINVAL;
			goto out;
		}

		slot_end_padding = frame_end_padding / 2;

		if (slot_end_padding > SOF_DAI_INTEL_SSP_SLOT_PADDING_MAX) {
			/* too big padding */
			trace_ssp_error("ssp_set_config() error: "
					"slot_end_padding > %d",
					SOF_DAI_INTEL_SSP_SLOT_PADDING_MAX);
			ret = -EINVAL;
			goto out;
		}

		sspsp |= SSPSP_DMYSTOP(slot_end_padding & SSPSP_DMYSTOP_MASK);
		slot_end_padding >>= SSPSP_DMYSTOP_BITS;
		sspsp |= SSPSP_EDMYSTOP(slot_end_padding & SSPSP_EDMYSTOP_MASK);

		break;

	case SOF_DAI_FMT_LEFT_J:

		/* default start_delay value is set to false */

		sscr0 |= SSCR0_FRDC(config->ssp.tdm_slots);

		/* LJDFD enable */
		sscr2 &= ~SSCR2_LJDFD;

		if (bdiv % 2) {
			trace_ssp_error("ssp_set_config() error: "
					"bdiv %d is not divisible by 2", bdiv);
			ret = -EINVAL;
			goto out;
		}

		/* set asserted frame length to half frame length */
		frame_len = bdiv / 2;

		/*
		 * handle frame polarity, LEFT_J default is rising/active high,
		 * non-inverted(inverted_frame=0) -- active high(SFRMP=1),
		 * inverted(inverted_frame=1) -- falling/active low(SFRMP=0),
		 * so, we should set SFRMP to !inverted_frame.
		 */
		sspsp |= SSPSP_SFRMP(!inverted_frame);

		/*
		 *  for I2S/LEFT_J, the padding has to happen at the end
		 * of each slot
		 */
		if (frame_end_padding % 2) {
			trace_ssp_error("ssp_set_config() error: "
					"frame_end_padding %d "
					"is not divisible by 2",
					frame_end_padding);
			ret = -EINVAL;
			goto out;
		}

		slot_end_padding = frame_end_padding / 2;

		if (slot_end_padding > 15) {
			/* can't handle padding over 15 bits */
			trace_ssp_error("ssp_set_config() error: "
					"slot_end_padding %d > 15 bits",
					slot_end_padding);
			ret = -EINVAL;
			goto out;
		}

		sspsp |= SSPSP_DMYSTOP(slot_end_padding & SSPSP_DMYSTOP_MASK);
		slot_end_padding >>= SSPSP_DMYSTOP_BITS;
		sspsp |= SSPSP_EDMYSTOP(slot_end_padding & SSPSP_EDMYSTOP_MASK);

		break;
	case SOF_DAI_FMT_DSP_A:

		start_delay = true;

		/* fallthrough */

	case SOF_DAI_FMT_DSP_B:

		/* default start_delay value is set to false */

		sscr0 |= SSCR0_MOD | SSCR0_FRDC(config->ssp.tdm_slots);

		/* set asserted frame length */
		frame_len = 1; /* default */

		if (cfs && ssp->params.frame_pulse_width > 0 &&
		    ssp->params.frame_pulse_width <=
		    SOF_DAI_INTEL_SSP_FRAME_PULSE_WIDTH_MAX) {
			frame_len = ssp->params.frame_pulse_width;
		}

		/* frame_pulse_width must less or equal 38 */
		if (ssp->params.frame_pulse_width >
			SOF_DAI_INTEL_SSP_FRAME_PULSE_WIDTH_MAX) {
			trace_ssp_error
				("ssp_set_config() error: "
				"frame_pulse_width > %d",
				SOF_DAI_INTEL_SSP_FRAME_PULSE_WIDTH_MAX);
			ret = -EINVAL;
			goto out;
		}
		/*
		 * handle frame polarity, DSP_B default is rising/active high,
		 * non-inverted(inverted_frame=0) -- active high(SFRMP=1),
		 * inverted(inverted_frame=1) -- falling/active low(SFRMP=0),
		 * so, we should set SFRMP to !inverted_frame.
		 */
		sspsp |= SSPSP_SFRMP(!inverted_frame);

		active_tx_slots = hweight_32(config->ssp.tx_slots);
		active_rx_slots = hweight_32(config->ssp.rx_slots);

		/*
		 * handle TDM mode, TDM mode has padding at the end of
		 * each slot. The amount of padding is equal to result of
		 * subtracting slot width and valid bits per slot.
		 */
		if (ssp->params.tdm_per_slot_padding_flag) {
			frame_end_padding = bdiv - config->ssp.tdm_slots *
				config->ssp.tdm_slot_width;

			slot_end_padding = config->ssp.tdm_slot_width -
				config->ssp.sample_valid_bits;

			if (slot_end_padding >
				SOF_DAI_INTEL_SSP_SLOT_PADDING_MAX) {
				trace_ssp_error
					("ssp_set_config() error: "
					"slot_end_padding > %d",
					SOF_DAI_INTEL_SSP_SLOT_PADDING_MAX);
				ret = -EINVAL;
				goto out;
			}

			sspsp |= SSPSP_DMYSTOP(slot_end_padding &
				SSPSP_DMYSTOP_MASK);
			slot_end_padding >>= SSPSP_DMYSTOP_BITS;
			sspsp |= SSPSP_EDMYSTOP(slot_end_padding &
				SSPSP_EDMYSTOP_MASK);
		}

		sspsp2 |= (frame_end_padding & SSPSP2_FEP_MASK);

		break;
	default:
		trace_ssp_error("ssp_set_config() error: "
				"invalid format 0x%04", config->format);
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

	/* setting TFT and RFT */
	switch (config->ssp.sample_valid_bits) {
	case 16:
			/* use 2 bytes for each slot */
			sample_width = 2;
			break;
	case 24:
	case 32:
			/* use 4 bytes for each slot */
			sample_width = 4;
			break;
	default:
			trace_ssp_error("ssp_set_config() error: "
					"sample_valid_bits %d",
					config->ssp.sample_valid_bits);
			ret = -EINVAL;
			goto out;
	}

	tft = MIN(SSP_FIFO_DEPTH - SSP_FIFO_WATERMARK,
		  sample_width * active_tx_slots);
	rft = MIN(SSP_FIFO_DEPTH - SSP_FIFO_WATERMARK,
		  sample_width * active_rx_slots);

	sscr3 |= SSCR3_TX(tft) | SSCR3_RX(rft);

	ssp_write(dai, SSCR0, sscr0);
	ssp_write(dai, SSCR1, sscr1);
	ssp_write(dai, SSCR2, sscr2);
	ssp_write(dai, SSCR3, sscr3);
	ssp_write(dai, SSPSP, sspsp);
	ssp_write(dai, SSPSP2, sspsp2);
	ssp_write(dai, SSIOC, ssioc);
	ssp_write(dai, SSTO, ssto);
	ssp_write(dai, SSTSA, sstsa);
	ssp_write(dai, SSRSA, ssrsa);

	trace_ssp("ssp_set_config(), sscr0 = 0x%08x, sscr1 = 0x%08x, "
		  "ssto = 0x%08x, sspsp = 0x%0x", sscr0, sscr1, ssto, sspsp);
	trace_ssp("ssp_set_config(), sscr2 = 0x%08x, sspsp2 = 0x%08x, "
		  "sscr3 = 0x%08x, ssioc = 0x%08x", sscr2, sspsp2, sscr3,
		  ssioc);
	trace_ssp("ssp_set_config(), ssrsa = 0x%08x, sstsa = 0x%08x", ssrsa,
		  sstsa);

	/* TODO: move this into M/N driver */
	mn_reg_write(0x0, mdivc);
	mn_reg_write(0x80 + config->ssp.mclk_id * 0x4, mdivr);
	mn_reg_write(0x100 + config->dai_index * 0x8 + 0x0, i2s_m);
	mn_reg_write(0x100 + config->dai_index * 0x8 + 0x4, i2s_n);

	ssp->state[DAI_DIR_PLAYBACK] = COMP_STATE_PREPARE;
	ssp->state[DAI_DIR_CAPTURE] = COMP_STATE_PREPARE;

out:
	spin_unlock(&dai->lock);

	return ret;
}

/* start the SSP for either playback or capture */
static void ssp_start(struct dai *dai, int direction)
{
	struct ssp_pdata *ssp = dai_get_drvdata(dai);

	spin_lock(&dai->lock);

	/* enable port */
	ssp_update_bits(dai, SSCR0, SSCR0_SSE, SSCR0_SSE);
	ssp->state[direction] = COMP_STATE_ACTIVE;

	trace_ssp("ssp_start()");

	/* enable DMA */
	if (direction == DAI_DIR_PLAYBACK) {
		ssp_update_bits(dai, SSCR1, SSCR1_TSRE, SSCR1_TSRE);
		ssp_update_bits(dai, SSTSA, 0x1 << 8, 0x1 << 8);
	} else {
		ssp_update_bits(dai, SSCR1, SSCR1_RSRE, SSCR1_RSRE);
		ssp_update_bits(dai, SSRSA, 0x1 << 8, 0x1 << 8);
	}

	/* wait to get valid fifo status */
	wait_delay(PLATFORM_SSP_DELAY);

	spin_unlock(&dai->lock);
}

/* stop the SSP for either playback or capture */
static void ssp_stop(struct dai *dai, int direction)
{
	struct ssp_pdata *ssp = dai_get_drvdata(dai);

	spin_lock(&dai->lock);

	/* wait to get valid fifo status */
	wait_delay(PLATFORM_SSP_DELAY);

	/* stop Rx if neeed */
	if (direction == DAI_DIR_CAPTURE &&
	    ssp->state[SOF_IPC_STREAM_CAPTURE] == COMP_STATE_ACTIVE) {
		ssp_update_bits(dai, SSCR1, SSCR1_RSRE, 0);
		ssp_update_bits(dai, SSRSA, 0x1 << 8, 0x0 << 8);
		ssp_empty_rx_fifo(dai);
		ssp->state[SOF_IPC_STREAM_CAPTURE] = COMP_STATE_PAUSED;
		trace_ssp("ssp_stop(), RX stop");
	}

	/* stop Tx if needed */
	if (direction == DAI_DIR_PLAYBACK &&
	    ssp->state[SOF_IPC_STREAM_PLAYBACK] == COMP_STATE_ACTIVE) {
		ssp_empty_tx_fifo(dai);
		ssp_update_bits(dai, SSCR1, SSCR1_TSRE, 0);
		ssp_update_bits(dai, SSTSA, 0x1 << 8, 0x0 << 8);
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

	trace_ssp("ssp_trigger() cmd %d", cmd);

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

	if (dai_get_drvdata(dai))
		return -EEXIST; /* already created */

	/* allocate private data */
	ssp = rzalloc(RZONE_SYS_RUNTIME | RZONE_FLAG_UNCACHED,
		      SOF_MEM_CAPS_RAM, sizeof(*ssp));
	if (!ssp) {
		trace_error(TRACE_CLASS_DAI, "ssp_probe() error: "
			    "alloc failed");
		return -ENOMEM;
	}
	dai_set_drvdata(dai, ssp);

	ssp->state[DAI_DIR_PLAYBACK] = COMP_STATE_READY;
	ssp->state[DAI_DIR_CAPTURE] = COMP_STATE_READY;

	/* Disable dynamic clock gating before touching any register */
	pm_runtime_get_sync(SSP_CLK, dai->index);

	ssp_empty_rx_fifo(dai);

	return 0;
}

static int ssp_remove(struct dai *dai)
{
	pm_runtime_put_sync(SSP_CLK, dai->index);

	rfree(dma_get_drvdata(dai));
	dai_set_drvdata(dai, NULL);

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
		.remove			= ssp_remove,
	},
};
