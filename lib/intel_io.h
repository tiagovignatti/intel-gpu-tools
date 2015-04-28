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

#ifndef INTEL_GPU_TOOLS_H
#define INTEL_GPU_TOOLS_H

#include <stdint.h>
#include <pciaccess.h>

/* register access helpers from intel_mmio.c */
extern void *igt_global_mmio;
void intel_mmio_use_pci_bar(struct pci_device *pci_dev);
void intel_mmio_use_dump_file(char *file);

int intel_register_access_init(struct pci_device *pci_dev, int safe);
void intel_register_access_fini(void);
uint32_t intel_register_read(uint32_t reg);
void intel_register_write(uint32_t reg, uint32_t val);
int intel_register_access_needs_fakewake(void);

uint32_t INREG(uint32_t reg);
uint16_t INREG16(uint32_t reg);
uint8_t INREG8(uint32_t reg);
void OUTREG(uint32_t reg, uint32_t val);
void OUTREG16(uint32_t reg, uint16_t val);
void OUTREG8(uint32_t reg, uint8_t val);

/* sideband access functions from intel_iosf.c */
uint32_t intel_dpio_reg_read(uint32_t reg, int phy);
void intel_dpio_reg_write(uint32_t reg, uint32_t val, int phy);
uint32_t intel_flisdsi_reg_read(uint32_t reg);
void intel_flisdsi_reg_write(uint32_t reg, uint32_t val);
uint32_t intel_iosf_sb_read(uint32_t port, uint32_t reg);
void intel_iosf_sb_write(uint32_t port, uint32_t reg, uint32_t val);

int intel_punit_read(uint32_t addr, uint32_t *val);
int intel_punit_write(uint32_t addr, uint32_t val);
int intel_nc_read(uint32_t addr, uint32_t *val);
int intel_nc_write(uint32_t addr, uint32_t val);

/* register maps from intel_reg_map.c */
#ifndef __GTK_DOC_IGNORE__

#define INTEL_RANGE_RSVD	(0<<0) /*  Shouldn't be read or written */
#define INTEL_RANGE_READ	(1<<0)
#define INTEL_RANGE_WRITE	(1<<1)
#define INTEL_RANGE_RW		(INTEL_RANGE_READ | INTEL_RANGE_WRITE)
#define INTEL_RANGE_END		(1<<31)

struct intel_register_range {
	uint32_t base;
	uint32_t size;
	uint32_t flags;
};

struct intel_register_map {
	struct intel_register_range *map;
	uint32_t top;
	uint32_t alignment_mask;
};
struct intel_register_map intel_get_register_map(uint32_t devid);
struct intel_register_range *intel_get_register_range(struct intel_register_map map, uint32_t offset, uint32_t mode);
#endif /* __GTK_DOC_IGNORE__ */

#endif /* INTEL_GPU_TOOLS_H */
