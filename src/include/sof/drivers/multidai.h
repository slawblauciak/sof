// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2019 Intel Corporation. All rights reserved.
//
// Author: Slawomir Blauciak <slawomir.blauciak@linux.intel.com>

#ifndef __SOF_DRIVERS_MULTIDAI_H__
#define __SOF_DRIVERS_MULTIDAI_H__

#include <ipc/channel_map.h>

#define MULTIDAI_MAX_LINKS	CHANNEL_MAP_MAX_LINKS

extern const struct dai_driver multidai_driver;

#endif /* __SOF_DRIVERS_MULTIDAI_H__ */
