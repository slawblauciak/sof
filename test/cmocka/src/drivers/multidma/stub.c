// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Slawomir Blauciak <slawomir.blauciak@linux.intel.com>

#include <sof/lib/alloc.h>
#include <mock_trace.h>
#include <stdlib.h>

TRACE_IMPL()

void *_balloc(int zone, uint32_t caps, size_t bytes, uint32_t alignment)
{
	(void)zone;
	(void)caps;

	return malloc(bytes);
}

void *_zalloc(int zone, uint32_t caps, size_t bytes)
{
	(void)zone;
	(void)caps;

	return calloc(bytes, 1);
}

void rfree(void *ptr)
{
	free(ptr);
}

void *_brealloc(void *ptr, int zone, uint32_t caps, size_t bytes,
		uint32_t alignment)
{
	(void)zone;
	(void)caps;

	return realloc(ptr, bytes);
}
