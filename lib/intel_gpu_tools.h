/*
 * Copyright © 2009 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */

#include <stdint.h>
#include <sys/types.h>
#include <pciaccess.h>

#include "intel_chipset.h"
#include "intel_reg.h"

#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof(arr[0]))

extern void *mmio;
void intel_get_mmio(struct pci_device *pci_dev);

/* New style register access API */
int intel_register_access_init(struct pci_device *pci_dev);
void intel_register_access_fini(void);
uint32_t intel_register_read(uint32_t reg);
void intel_register_write(uint32_t reg, uint32_t val);

static inline uint32_t
INREG(uint32_t reg)
{
	return *(volatile uint32_t *)((volatile char *)mmio + reg);
}

static inline void
OUTREG(uint32_t reg, uint32_t val)
{
	*(volatile uint32_t *)((volatile char *)mmio + reg) = val;
}

struct pci_device *intel_get_pci_device(void);

uint32_t intel_get_drm_devid(int fd);

void intel_map_file(char *);

enum pch_type {
	PCH_IBX,
	PCH_CPT,
};

extern enum pch_type pch;
void intel_check_pch(void);

#define HAS_CPT (pch == PCH_CPT)
