/*
 * Copyright (c) 2017, Intel Corporation
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
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 *         Keyon Jie <yang.jie@linux.intel.com>
 */

#include <platform/shim.h>
#include <platform/clk.h>
#include <platform/timer.h>
#include <platform/cavs/platform_common.h>
#include <uapi/ipc.h>
#include <sof/dai.h>
#include <sof/dma.h>
#include <sof/agent.h>
#include <sof/work.h>
#include <sof/clock.h>
#include <sof/io.h>
#include <sof/ipc.h>
#include <sof/trace.h>

int platform_init(struct sof *sof)
{
	struct dai *ssp;
	struct dai *dmic0;
	int i, ret;

	platform_interrupt_init();

	trace_point(TRACE_BOOT_PLATFORM_MBOX);
	platform_memory_windows_init();

	trace_point(TRACE_BOOT_PLATFORM_SHIM);

	/* init work queues and clocks */
	trace_point(TRACE_BOOT_PLATFORM_TIMER);
	platform_timer_start(&platform_ext_timer);

	trace_point(TRACE_BOOT_PLATFORM_CLOCK);
	init_platform_clocks();

	trace_point(TRACE_BOOT_SYS_WORK);
	init_system_workq(&platform_generic_queue);

	/* init the system agent */
	sa_init(sof);

	/* Set CPU to default frequency for booting */
	trace_point(TRACE_BOOT_SYS_CPU_FREQ);
	clock_set_freq(CLK_CPU, CLK_MAX_CPU_HZ);

	/* set SSP clock to 19.2M */
	trace_point(TRACE_BOOT_PLATFORM_SSP_FREQ);
	clock_set_freq(CLK_SSP, 19200000);

	/* initialise the host IPC mechanisms */
	trace_point(TRACE_BOOT_PLATFORM_IPC);
	ipc_init(sof);

	/* disable PM for boot */
	shim_write(SHIM_CLKCTL, shim_read(SHIM_CLKCTL) |
		SHIM_CLKCTL_LPGPDMAFDCGB(0) |
		SHIM_CLKCTL_LPGPDMAFDCGB(1) |
		SHIM_CLKCTL_I2SFDCGB(3) |
		SHIM_CLKCTL_I2SFDCGB(2) |
		SHIM_CLKCTL_I2SFDCGB(1) |
		SHIM_CLKCTL_I2SFDCGB(0) |
		SHIM_CLKCTL_DMICFDCGB |
		SHIM_CLKCTL_I2SEFDCGB(1) |
		SHIM_CLKCTL_I2SEFDCGB(0) |
		SHIM_CLKCTL_TCPAPLLS |
		SHIM_CLKCTL_RAPLLC |
		SHIM_CLKCTL_RXOSCC |
		SHIM_CLKCTL_RFROSCC |
		SHIM_CLKCTL_TCPLCG(0) | SHIM_CLKCTL_TCPLCG(1));

	shim_write(SHIM_LPSCTL, shim_read(SHIM_LPSCTL));

	/* init DMACs */
	trace_point(TRACE_BOOT_PLATFORM_DMA);
	ret = dmac_init();
	if (ret < 0)
		return -ENODEV;


	/* init SSP ports */
	trace_point(TRACE_BOOT_PLATFORM_SSP);
	for (i = 0; i < PLATFORM_NUM_SSP; i++) {
		ssp = dai_get(SOF_DAI_INTEL_SSP, i);
		if (ssp == NULL)
			return -ENODEV;
		dai_probe(ssp);
	}

	/* Init DMIC. Note that the two PDM controllers and four microphones
	 * supported max. those are available in platform are handled by dmic0.
	 */
	trace_point(TRACE_BOOT_PLATFORM_DMIC);
	dmic0 = dai_get(SOF_DAI_INTEL_DMIC, 0);
	if (!dmic0)
		return -ENODEV;

	dai_probe(dmic0);

	/* Initialize DMA for Trace*/
	dma_trace_init_complete(sof->dmat);

	return 0;
}
