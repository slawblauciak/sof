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

#include <platform/clk.h>
#include <platform/interrupt.h>
#include <platform/memory.h>
#include <platform/platform.h>
#include <platform/timer.h>
#include <platform/cavs/platform_common.h>
#include <sof/alloc.h>
#include <sof/clock.h>
#include <sof/io.h>
#include <sof/notifier.h>
#include <sof/work.h>
#include <uapi/ipc.h>
#include <config.h>
#include <version.h>

static const struct sof_ipc_fw_ready ready = {
	.hdr = {
		.cmd = SOF_IPC_FW_READY,
		.size = sizeof(struct sof_ipc_fw_ready),
	},
	.version = {
		.build = SOF_BUILD,
		.minor = SOF_MINOR,
		.major = SOF_MAJOR,
		.date = __DATE__,
		.time = __TIME__,
		.tag = SOF_TAG,
	},
};

#define SRAM_WINDOW_HOST_OFFSET(x)		(0x80000 + x * 0x20000)

static const struct sof_ipc_window sram_window = {
	.ext_hdr	= {
		.hdr.cmd = SOF_IPC_FW_READY,
		.hdr.size = sizeof(struct sof_ipc_window) +
			sizeof(struct sof_ipc_window_elem) *
				PLATFORM_NUM_IPC_WINDOWS,
		.type	= SOF_IPC_EXT_WINDOW,
	},
	.num_windows	= PLATFORM_NUM_IPC_WINDOWS,
	.window	= {
		{
			.type	= SOF_IPC_REGION_REGS,
			.id	= 0,	/* map to host window 0 */
			.flags	= 0, // TODO: set later
			.size	= MAILBOX_SW_REG_SIZE,
			.offset	= 0,
		},
		{
			.type	= SOF_IPC_REGION_UPBOX,
			.id	= 0,	/* map to host window 0 */
			.flags	= 0, // TODO: set later
			.size	= MAILBOX_DSPBOX_SIZE,
			.offset	= MAILBOX_SW_REG_SIZE,
		},
		{
			.type	= SOF_IPC_REGION_DOWNBOX,
			.id	= 1,	/* map to host window 1 */
			.flags	= 0, // TODO: set later
			.size	= MAILBOX_HOSTBOX_SIZE,
			.offset	= 0,
		},
		{
			.type	= SOF_IPC_REGION_DEBUG,
			.id	= 2,	/* map to host window 2 */
			.flags	= 0, // TODO: set later
			.size	= MAILBOX_EXCEPTION_SIZE + MAILBOX_DEBUG_SIZE,
			.offset	= 0,
		},
		{
			.type	= SOF_IPC_REGION_EXCEPTION,
			.id	= 2,	/* map to host window 2 */
			.flags	= 0, // TODO: set later
			.size	= MAILBOX_EXCEPTION_SIZE,
			.offset	= MAILBOX_EXCEPTION_OFFSET,
		},
		{
			.type	= SOF_IPC_REGION_STREAM,
			.id	= 2,	/* map to host window 2 */
			.flags	= 0, // TODO: set later
			.size	= MAILBOX_STREAM_SIZE,
			.offset	= MAILBOX_STREAM_OFFSET,
		},
		{
			.type	= SOF_IPC_REGION_TRACE,
			.id	= 3,	/* map to host window 3 */
			.flags	= 0, // TODO: set later
			.size	= MAILBOX_TRACE_SIZE,
			.offset	= 0,
		},
	},
};

struct work_queue_timesource platform_generic_queue = {
	.timer	 = {
		.id = TIMER3, /* external timer */
		.irq = IRQ_EXT_TSTAMP0_LVL2(0),
	},
	.clk		= CLK_SSP,
	.notifier	= NOTIFIER_ID_SSP_FREQ,
	.timer_set	= platform_timer_set,
	.timer_clear	= platform_timer_clear,
	.timer_get	= platform_timer_get,
};

struct timer *platform_timer = &platform_generic_queue.timer;

int platform_boot_complete(uint32_t boot_message)
{
	mailbox_dspbox_write(0, &ready, sizeof(ready));
	mailbox_dspbox_write(sizeof(ready), &sram_window,
		sram_window.ext_hdr.hdr.size);

#ifdef PLATFORM_POST_BOOT_CPU_FREQ_RESET
	/* boot now complete so we can relax the CPU */
	clock_set_freq(CLK_CPU, CLK_DEFAULT_CPU_HZ);
#endif

	/* tell host we are ready */
	ipc_write(PLATFORM_RDY_IPC_REG1, SRAM_WINDOW_HOST_OFFSET(0) >> 12);
	ipc_write(PLATFORM_RDY_IPC_REG2, 0x80000000 | SOF_IPC_FW_READY);

	return 0;
}

void platform_memory_windows_init(void)
{
	/* window0, for fw status & outbox/uplink mbox */
	io_reg_write(DMWLO(0), HP_SRAM_WIN0_SIZE | 0x7);
	io_reg_write(DMWBA(0), HP_SRAM_WIN0_BASE
		| DMWBA_READONLY | DMWBA_ENABLE);
	bzero((void *)(HP_SRAM_WIN0_BASE + SRAM_REG_FW_END),
	      HP_SRAM_WIN0_SIZE - SRAM_REG_FW_END);
	dcache_writeback_region((void *)(HP_SRAM_WIN0_BASE + SRAM_REG_FW_END),
				HP_SRAM_WIN0_SIZE - SRAM_REG_FW_END);

	/* window1, for inbox/downlink mbox */
	io_reg_write(DMWLO(1), HP_SRAM_WIN1_SIZE | 0x7);
	io_reg_write(DMWBA(1), HP_SRAM_WIN1_BASE
		| DMWBA_ENABLE);
	bzero((void *)HP_SRAM_WIN1_BASE, HP_SRAM_WIN1_SIZE);
	dcache_writeback_region((void *)HP_SRAM_WIN1_BASE, HP_SRAM_WIN1_SIZE);

	/* window2, for debug */
	io_reg_write(DMWLO(2), HP_SRAM_WIN2_SIZE | 0x7);
	io_reg_write(DMWBA(2), HP_SRAM_WIN2_BASE
		| DMWBA_READONLY | DMWBA_ENABLE);
	bzero((void *)HP_SRAM_WIN2_BASE, HP_SRAM_WIN2_SIZE);
	dcache_writeback_region((void *)HP_SRAM_WIN2_BASE, HP_SRAM_WIN2_SIZE);

	/* window3, for trace */
	io_reg_write(DMWLO(3), HP_SRAM_WIN3_SIZE | 0x7);
	io_reg_write(DMWBA(3), HP_SRAM_WIN3_BASE
		| DMWBA_READONLY | DMWBA_ENABLE);
	bzero((void *)HP_SRAM_WIN3_BASE, HP_SRAM_WIN3_SIZE);
	dcache_writeback_region((void *)HP_SRAM_WIN3_BASE, HP_SRAM_WIN3_SIZE);
}

struct timer platform_ext_timer = {
	.id = TIMER3,
	.irq = IRQ_EXT_TSTAMP0_LVL2(0),
};
