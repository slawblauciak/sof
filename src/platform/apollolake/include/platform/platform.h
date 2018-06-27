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
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 *         Keyon Jie <yang.jie@linux.intel.com>
 *         Xiuli Pan <xiuli.pan@linux.intel.com>
 */

#ifndef __PLATFORM_PLATFORM_H__
#define __PLATFORM_PLATFORM_H__

#include <sof/platform.h>
#include <platform/platcfg.h>
#include <platform/shim.h>
#include <platform/interrupt.h>
#include <uapi/ipc.h>

struct sof;

/* Host page size */
#define HOST_PAGE_SIZE		4096
#define PLATFORM_PAGE_TABLE_SIZE	256

/* IPC Interrupt */
#define PLATFORM_IPC_INTERUPT	IRQ_EXT_IPC_LVL2(0)

/* pipeline IRQ */
#define PLATFORM_SCHEDULE_IRQ	IRQ_NUM_SOFTWARE4

#define PLATFORM_IRQ_TASK_HIGH	IRQ_NUM_SOFTWARE3
#define PLATFORM_IRQ_TASK_MED	IRQ_NUM_SOFTWARE2
#define PLATFORM_IRQ_TASK_LOW	IRQ_NUM_SOFTWARE1

#define PLATFORM_SCHEDULE_COST	200

/* maximum preload pipeline depth */
#define MAX_PRELOAD_SIZE	20

/* DMA treats PHY addresses as host address unless within DSP region */
#define PLATFORM_HOST_DMA_MASK	0x00000000

/* Platform stream capabilities */
#define PLATFORM_MAX_CHANNELS	4
#define PLATFORM_MAX_STREAMS	16

/* clock source used by scheduler for deadline calculations */
#define PLATFORM_SCHED_CLOCK	CLK_SSP

/* DMA channel drain timeout in microseconds - TODO: caclulate based on topology */
#define PLATFORM_DMA_TIMEOUT	1333

/* DMA host transfer timeouts in microseconds */
#define PLATFORM_HOST_DMA_TIMEOUT	50

/* WorkQ window size in microseconds */
#define PLATFORM_WORKQ_WINDOW	2000

/* platform WorkQ clock */
#define PLATFORM_WORKQ_CLOCK	CLK_SSP

/* Host finish work schedule delay in microseconds */
#define PLATFORM_HOST_FINISH_DELAY	100

/* Host finish work(drain from host to dai) timeout in microseconds */
#define PLATFORM_HOST_FINISH_TIMEOUT	50000

/* local buffer size of DMA tracing */
#define DMA_TRACE_LOCAL_SIZE	HOST_PAGE_SIZE

/* trace bytes flushed during panic */
#define DMA_FLUSH_TRACE_SIZE    (MAILBOX_TRACE_SIZE >> 2)

/* the interval of DMA trace copying */
#define DMA_TRACE_PERIOD		500000

/*
 * the interval of reschedule DMA trace copying in special case like half
 * fullness of local DMA trace buffer
 */
#define DMA_TRACE_RESCHEDULE_TIME	5000

/* DSP should be idle in this time frame */
#define PLATFORM_IDLE_TIME	750000

/* platform has DMA memory type */
#define PLATFORM_MEM_HAS_DMA

/* platform has low power memory type */
#define PLATFORM_MEM_HAS_LP_RAM

/* number of SSP ports in platform */
#define PLATFORM_NUM_SSP	6

/* DSP default delay in cycles */
#define PLATFORM_DEFAULT_DELAY	12

/* minimal L1 exit time in cycles */
#define PLATFORM_FORCE_L1_EXIT_TIME	585

/* number of IPC windows */
#define PLATFORM_NUM_IPC_WINDOWS 7

/* the CPU frequency should be reset to default after the boot sequence */
#define PLATFORM_POST_BOOT_CPU_FREQ_RESET

/* the first IPC register to inform the driver of FW readiness */
#define PLATFORM_RDY_IPC_REG1 IPC_DIPCIE

/* the second IPC register to inform the driver of FW readiness */
#define PLATFORM_RDY_IPC_REG2 IPC_DIPCI

/* SSP frequency in Hz */
#define PLATFORM_SSP_FREQ 19200000

/* Platform defined panic code */
static inline void platform_panic(uint32_t p)
{
	mailbox_sw_reg_write(SRAM_REG_FW_STATUS, p & 0x3fffffff);
	ipc_write(IPC_DIPCIE, MAILBOX_EXCEPTION_OFFSET + 2 * 0x20000);
	ipc_write(IPC_DIPCI, 0x80000000 | (p & 0x3fffffff));
}

/* Platform defined trace code */
#define platform_trace_point(__x) \
	mailbox_sw_reg_write(SRAM_REG_FW_TRACEP, (__x))

extern struct timer *platform_timer;

/*
 * APIs declared here are defined for every platform and IPC mechanism.
 */

int platform_ssp_set_mn(uint32_t ssp_port, uint32_t source, uint32_t rate,
	uint32_t bclk_fs);

void platform_ssp_disable_mn(uint32_t ssp_port);

#endif
