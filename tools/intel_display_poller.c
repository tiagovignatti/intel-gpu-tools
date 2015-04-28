/*
 * Copyright © 2014 Intel Corporation
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
 */

#include <assert.h>
#include <fcntl.h>
#include <getopt.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <string.h>
#include "intel_chipset.h"
#include "intel_io.h"
#include "igt_debugfs.h"
#include "drmtest.h"
#include "igt_aux.h"

enum test {
	TEST_INVALID,
	TEST_PIPESTAT,
	TEST_IIR,
	TEST_IIR_GEN2,
	TEST_IIR_GEN3,
	TEST_DEIIR,
	TEST_FRAMECOUNT,
	TEST_FRAMECOUNT_GEN3,
	TEST_FRAMECOUNT_G4X,
	TEST_FLIPCOUNT,
	TEST_PAN,
	TEST_FLIP,
	TEST_SURFLIVE,
	TEST_WRAP,
	TEST_FIELD,
};

static uint32_t vlv_offset;

static volatile bool quit;

static void sighandler(int x)
{
	quit = true;
}

static uint16_t read_reg_16(uint32_t reg)
{
	return INREG16(vlv_offset + reg);
}

static uint32_t read_reg(uint32_t reg)
{
	return INREG(vlv_offset + reg);
}

static void write_reg_16(uint32_t reg, uint16_t val)
{
	OUTREG16(vlv_offset + reg, val);
}

static void write_reg(uint32_t reg, uint32_t val)
{
	OUTREG(vlv_offset + reg, val);
}

static int pipe_to_plane(uint32_t devid, int pipe)
{
	if (!IS_GEN2(devid) && !IS_GEN3(devid))
		return pipe;

	switch (pipe) {
	case 0:
		if ((read_reg(DSPACNTR) & DISPPLANE_SEL_PIPE_MASK) == DISPPLANE_SEL_PIPE_B)
			return 1;
		return 0;
	case 1:
		if ((read_reg(DSPACNTR) & DISPPLANE_SEL_PIPE_MASK) == DISPPLANE_SEL_PIPE_A)
			return 0;
		return 1;
	}

	assert(0);

	return 0;
}

static uint32_t dspoffset_reg(uint32_t devid, int pipe)
{
	bool use_tileoff;

	pipe = pipe_to_plane(devid, pipe);

	if (IS_GEN2(devid) || IS_GEN3(devid))
		use_tileoff = false;
	if (IS_HASWELL(devid) || IS_BROADWELL(devid))
		use_tileoff = true;
	else {
		switch (pipe) {
		case 0:
			use_tileoff = read_reg(DSPACNTR) & DISPLAY_PLANE_TILED;
			break;
		case 1:
			use_tileoff = read_reg(DSPBCNTR) & DISPLAY_PLANE_TILED;
			break;
		case 2:
			use_tileoff = read_reg(DSPCCNTR) & DISPLAY_PLANE_TILED;
			break;
		}
	}

	if (use_tileoff) {
		switch (pipe) {
		case 0:
			return DSPATILEOFF;
		case 1:
			return DSPBTILEOFF;
		case 2:
			return DSPCTILEOFF;
		}
	} else {
		switch (pipe) {
		case 0:
			return DSPABASE;
		case 1:
			return DSPBBASE;
		case 2:
			return DSPCBASE;
		}
	}

	assert(0);

	return 0;
}

static uint32_t dspsurf_reg(uint32_t devid, int pipe)
{
	pipe = pipe_to_plane(devid, pipe);

	if (IS_GEN2(devid) || IS_GEN3(devid)) {
		switch (pipe) {
		case 0:
			return DSPABASE;
		case 1:
			return DSPBBASE;
		case 2:
			return DSPCBASE;
		}
	} else {
		switch (pipe) {
		case 0:
			return DSPASURF;
		case 1:
			return DSPBSURF;
		case 2:
			return DSPCSURF;
		}
	}

	assert(0);

	return 0;
}

static uint32_t dsl_reg(int pipe)
{
	switch (pipe) {
	case 0:
		return PIPEA_DSL;
	case 1:
		return PIPEB_DSL;
	case 2:
		return PIPEC_DSL;
	}

	assert(0);

	return 0;
}

static void poll_pixel_pipestat(int pipe, int bit, uint32_t *min, uint32_t *max, const int count)
{
	uint32_t pix, pix1, pix2, iir, iir1, iir2, iir_bit, iir_mask;
	int i = 0;

	switch (pipe) {
	case 0:
		pix = PIPEAFRAMEPIXEL;
		iir_bit = 1 << bit;
		iir = PIPEASTAT;
		break;
	case 1:
		pix = PIPEBFRAMEPIXEL;
		iir_bit = 1 << bit;
		iir = PIPEBSTAT;
		break;
	default:
		return;
	}

	iir_mask = read_reg(iir) & 0x7fff0000;

	write_reg(iir, iir_mask | iir_bit);

	while (!quit) {
		pix1 = read_reg(pix);
		iir1 = read_reg(iir);
		iir2 = read_reg(iir);
		pix2 = read_reg(pix);

		if (!(iir2 & iir_bit))
			continue;

		if (iir1 & iir_bit) {
			write_reg(iir, iir_mask | iir_bit);
			continue;
		}

		pix1 &= PIPE_PIXEL_MASK;
		pix2 &= PIPE_PIXEL_MASK;

		min[i] = pix1;
		max[i] = pix2;
		if (++i >= count)
			break;
	}
}

static void poll_pixel_iir_gen3(int pipe, int bit, uint32_t *min, uint32_t *max, const int count)
{
	uint32_t pix, pix1, pix2, iir1, iir2, imr_save, ier_save;
	int i = 0;

	bit = 1 << bit;

	switch (pipe) {
	case 0:
		pix = PIPEAFRAMEPIXEL;
		break;
	case 1:
		pix = PIPEBFRAMEPIXEL;
		break;
	default:
		return;
	}

	imr_save = read_reg(IMR);
	ier_save = read_reg(IER);

	write_reg(IER, ier_save & ~bit);
	write_reg(IMR, imr_save & ~bit);

	write_reg(IIR, bit);

	while (!quit) {
		pix1 = read_reg(pix);
		iir1 = read_reg(IIR);
		iir2 = read_reg(IIR);
		pix2 = read_reg(pix);

		if (!(iir2 & bit))
			continue;

		write_reg(IIR, bit);

		if (iir1 & bit)
			continue;

		pix1 &= PIPE_PIXEL_MASK;
		pix2 &= PIPE_PIXEL_MASK;

		min[i] = pix1;
		max[i] = pix2;
		if (++i >= count)
			break;
	}

	write_reg(IMR, imr_save);
	write_reg(IER, ier_save);
}

static void poll_pixel_framecount_gen3(int pipe, uint32_t *min, uint32_t *max, const int count)
{
	uint32_t pix, pix1, pix2, frm1, frm2;
	int i = 0;

	switch (pipe) {
	case 0:
		pix = PIPEAFRAMEPIXEL;
		break;
	case 1:
		pix = PIPEBFRAMEPIXEL;
		break;
	default:
		return;
	}

	while (!quit) {
		pix1 = read_reg(pix);
		pix2 = read_reg(pix);

		frm1 = pix1 >> 24;
		frm2 = pix2 >> 24;

		if (frm1 + 1 != frm2)
			continue;

		pix1 &= PIPE_PIXEL_MASK;
		pix2 &= PIPE_PIXEL_MASK;

		min[i] = pix1;
		max[i] = pix2;
		if (++i >= count)
			break;
	}
}

static void poll_pixel_pan(uint32_t devid, int pipe, int target_pixel, int target_fuzz,
			   uint32_t *min, uint32_t *max, const int count)
{
	uint32_t pix, pix1 = 0, pix2 = 0;
	uint32_t saved, surf = 0;
	int i = 0;

	switch (pipe) {
	case 0:
		pix = PIPEAFRAMEPIXEL;
		break;
	case 1:
		pix = PIPEBFRAMEPIXEL;
		break;
	default:
		return;
	}

	surf = dspoffset_reg(devid, pipe);

	saved = read_reg(surf);

	while (!quit) {
		while (!quit){
			pix1 = read_reg(pix) & PIPE_PIXEL_MASK;
			if (pix1 == target_pixel)
				break;
		}

		write_reg(surf, saved+256);

		while (!quit){
			pix2 = read_reg(pix) & PIPE_PIXEL_MASK;
			if (pix2 >= target_pixel + target_fuzz)
				break;
		}

		write_reg(surf, saved);

		min[i] = pix1;
		max[i] = pix2;
		if (++i >= count)
			break;
	}

	write_reg(surf, saved);
}

static void poll_pixel_flip(uint32_t devid, int pipe, int target_pixel, int target_fuzz,
			    uint32_t *min, uint32_t *max, const int count)
{
	uint32_t pix, pix1, pix2;
	uint32_t saved, surf = 0;
	int i = 0;

	switch (pipe) {
	case 0:
		pix = PIPEAFRAMEPIXEL;
	case 1:
		pix = PIPEBFRAMEPIXEL;
	default:
		return;
	}

	surf = dspsurf_reg(devid, pipe);

	saved = read_reg(surf);

	while (!quit) {
		while (!quit){
			pix1 = read_reg(pix) & PIPE_PIXEL_MASK;
			if (pix1 == target_pixel)
				break;
		}

		write_reg(surf, saved+4096);

		while (!quit){
			pix2 = read_reg(pix) & PIPE_PIXEL_MASK;
			if (pix2 >= target_pixel + target_fuzz)
				break;
		}

		write_reg(surf, saved);

		min[i] = pix1;
		max[i] = pix2;
		if (++i >= count)
			break;
	}

	write_reg(surf, saved);
}

static void poll_pixel_wrap(int pipe, uint32_t *min, uint32_t *max, const int count)
{
	uint32_t pix, pix1, pix2;
	int i = 0;

	switch (pipe) {
	case 0:
		pix = PIPEAFRAMEPIXEL;
		break;
	case 1:
		pix = PIPEBFRAMEPIXEL;
		break;
	default:
		return;
	}

	while (!quit) {
		pix1 = read_reg(pix);
		pix2 = read_reg(pix);

		pix1 &= PIPE_PIXEL_MASK;
		pix2 &= PIPE_PIXEL_MASK;

		if (pix2 >= pix1)
			continue;

		min[i] = pix1;
		max[i] = pix2;
		if (++i >= count)
			break;
	}
}

static void poll_dsl_pipestat(int pipe, int bit,
			      uint32_t *min, uint32_t *max, const int count)
{
	uint32_t dsl, dsl1, dsl2, iir, iir1, iir2, iir_bit, iir_mask;
	bool field1, field2;
	int i[2] = {};

	switch (pipe) {
	case 0:
		iir_bit = 1 << bit;
		iir = PIPEASTAT;
		break;
	case 1:
		iir_bit = 1 << bit;
		iir = PIPEBSTAT;
		break;
	default:
		return;
	}

	dsl = dsl_reg(pipe);

	iir_mask = read_reg(iir) & 0x7fff0000;

	write_reg(iir, iir_mask | iir_bit);

	while (!quit) {
		dsl1 = read_reg(dsl);
		iir1 = read_reg(iir);
		iir2 = read_reg(iir);
		dsl2 = read_reg(dsl);

		field1 = dsl1 & 0x80000000;
		field2 = dsl2 & 0x80000000;
		dsl1 &= ~0x80000000;
		dsl2 &= ~0x80000000;

		if (!(iir2 & iir_bit))
			continue;

		if (iir1 & iir_bit) {
			write_reg(iir, iir_mask | iir_bit);
			continue;
		}

		if (field1 != field2)
			printf("fields are different (%u:%u -> %u:%u)\n",
			       field1, dsl1, field2, dsl2);

		min[field1*count+i[field1]] = dsl1;
		max[field1*count+i[field1]] = dsl2;
		if (++i[field1] >= count)
			break;
	}
}

static void poll_dsl_iir_gen2(int pipe, int bit,
			      uint32_t *min, uint32_t *max, const int count)
{
	uint32_t dsl, dsl1, dsl2, iir1, iir2, imr_save, ier_save;
	bool field1, field2;
	int i[2] = {};

	bit = 1 << bit;

	dsl = dsl_reg(pipe);

	imr_save = read_reg_16(IMR);
	ier_save = read_reg_16(IER);

	write_reg_16(IER, ier_save & ~bit);
	write_reg_16(IMR, imr_save & ~bit);

	write_reg_16(IIR, bit);

	while (!quit) {
		dsl1 = read_reg(dsl);
		iir1 = read_reg_16(IIR);
		iir2 = read_reg_16(IIR);
		dsl2 = read_reg(dsl);

		field1 = dsl1 & 0x80000000;
		field2 = dsl2 & 0x80000000;
		dsl1 &= ~0x80000000;
		dsl2 &= ~0x80000000;

		if (!(iir2 & bit))
			continue;

		write_reg_16(IIR, bit);

		if (iir1 & bit)
			continue;

		if (field1 != field2)
			printf("fields are different (%u:%u -> %u:%u)\n",
			       field1, dsl1, field2, dsl2);

		min[field1*count+i[field1]] = dsl1;
		max[field1*count+i[field1]] = dsl2;
		if (++i[field1] >= count)
			break;
	}

	write_reg_16(IMR, imr_save);
	write_reg_16(IER, ier_save);
}

static void poll_dsl_iir_gen3(int pipe, int bit,
			      uint32_t *min, uint32_t *max, const int count)
{
	uint32_t dsl, dsl1, dsl2, iir1, iir2, imr_save, ier_save;
	bool field1, field2;
	int i[2] = {};

	bit = 1 << bit;

	dsl = dsl_reg(pipe);

	imr_save = read_reg(IMR);
	ier_save = read_reg(IER);

	write_reg(IER, ier_save & ~bit);
	write_reg(IMR, imr_save & ~bit);

	write_reg(IIR, bit);

	while (!quit) {
		dsl1 = read_reg(dsl);
		iir1 = read_reg(IIR);
		iir2 = read_reg(IIR);
		dsl2 = read_reg(dsl);

		field1 = dsl1 & 0x80000000;
		field2 = dsl2 & 0x80000000;
		dsl1 &= ~0x80000000;
		dsl2 &= ~0x80000000;

		if (!(iir2 & bit))
			continue;

		write_reg(IIR, bit);

		if (iir1 & bit)
			continue;

		if (field1 != field2)
			printf("fields are different (%u:%u -> %u:%u)\n",
			       field1, dsl1, field2, dsl2);

		min[field1*count+i[field1]] = dsl1;
		max[field1*count+i[field1]] = dsl2;
		if (++i[field1] >= count)
			break;
	}

	write_reg(IMR, imr_save);
	write_reg(IER, ier_save);
}

static void poll_dsl_deiir(uint32_t devid, int pipe, int bit,
			   uint32_t *min, uint32_t *max, const int count)
{
	uint32_t dsl, dsl1, dsl2, iir1, iir2, imr_save, ier_save;
	bool field1, field2;
	uint32_t iir, ier, imr;
	int i[2] = {};

	bit = 1 << bit;

	dsl = dsl_reg(pipe);

	if (IS_GEN8(devid)) {
		iir = GEN8_DE_PIPE_IIR(pipe);
		ier = GEN8_DE_PIPE_IER(pipe);
		imr = GEN8_DE_PIPE_IMR(pipe);
	} else {
		iir = DEIIR;
		ier = DEIER;
		imr = DEIMR;
	}

	imr_save = read_reg(imr);
	ier_save = read_reg(ier);

	write_reg(ier, ier_save & ~bit);
	write_reg(imr, imr_save & ~bit);

	write_reg(iir, bit);

	while (!quit) {
		dsl1 = read_reg(dsl);
		iir1 = read_reg(iir);
		iir2 = read_reg(iir);
		dsl2 = read_reg(dsl);

		field1 = dsl1 & 0x80000000;
		field2 = dsl2 & 0x80000000;
		dsl1 &= ~0x80000000;
		dsl2 &= ~0x80000000;

		if (!(iir2 & bit))
			continue;

		write_reg(iir, bit);

		if (iir1 & bit)
			continue;

		if (field1 != field2)
			printf("fields are different (%u:%u -> %u:%u)\n",
			       field1, dsl1, field2, dsl2);

		min[field1*count+i[field1]] = dsl1;
		max[field1*count+i[field1]] = dsl2;
		if (++i[field1] >= count)
			break;
	}

	write_reg(imr, imr_save);
	write_reg(ier, ier_save);
}

static void poll_dsl_framecount_g4x(int pipe, uint32_t *min, uint32_t *max, const int count)
{
	uint32_t dsl, dsl1, dsl2, frm, frm1, frm2;
	bool field1, field2;
	int i[2] = {};

	switch (pipe) {
	case 0:
		frm = PIPEAFRMCOUNT_G4X;
		break;
	case 1:
		frm = PIPEBFRMCOUNT_G4X;
		break;
	case 2:
		frm = PIPECFRMCOUNT_G4X;
		break;
	default:
		return;
	}

	dsl = dsl_reg(pipe);

	while (!quit) {
		dsl1 = read_reg(dsl);
		frm1 = read_reg(frm);
		frm2 = read_reg(frm);
		dsl2 = read_reg(dsl);

		field1 = dsl1 & 0x80000000;
		field2 = dsl2 & 0x80000000;
		dsl1 &= ~0x80000000;
		dsl2 &= ~0x80000000;

		if (frm1 + 1 != frm2)
			continue;

		if (field1 != field2)
			printf("fields are different (%u:%u -> %u:%u)\n",
			       field1, dsl1, field2, dsl2);

		min[field1*count+i[field1]] = dsl1;
		max[field1*count+i[field1]] = dsl2;
		if (++i[field1] >= count)
			break;
	}
}

static void poll_dsl_flipcount_g4x(uint32_t devid, int pipe,
				   uint32_t *min, uint32_t *max, const int count)
{
	uint32_t dsl, dsl1, dsl2, flp, flp1, flp2, surf;
	bool field1, field2;
	int i[2] = {};

	switch (pipe) {
	case 0:
		flp = PIPEAFLIPCOUNT_G4X;
		break;
	case 1:
		flp = PIPEBFLIPCOUNT_G4X;
		break;
	case 2:
		flp = PIPECFLIPCOUNT_G4X;
		break;
	default:
		return;
	}

	dsl = dsl_reg(pipe);
	surf = dspsurf_reg(devid, pipe);

	while (!quit) {
		usleep(10);
		dsl1 = read_reg(dsl);
		flp1 = read_reg(flp);
		dsl2 = read_reg(dsl);

		field1 = dsl1 & 0x80000000;
		field2 = dsl2 & 0x80000000;
		dsl1 &= ~0x80000000;
		dsl2 &= ~0x80000000;

		if (field1 != field2)
			printf("fields are different (%u:%u -> %u:%u)\n",
			       field1, dsl1, field2, dsl2);

		min[field1*count+i[field1]] = dsl1;
		max[field1*count+i[field1]] = dsl2;
		if (++i[field1] >= count)
			return;

		write_reg(surf, read_reg(surf));

		while (!quit) {
			dsl1 = read_reg(dsl);
			flp2 = read_reg(flp);
			dsl2 = read_reg(dsl);

			field1 = dsl1 & 0x80000000;
			field2 = dsl2 & 0x80000000;
			dsl1 &= ~0x80000000;
			dsl2 &= ~0x80000000;

			if (flp1 == flp2)
				continue;

			if (field1 != field2)
				printf("fields are different (%u:%u -> %u:%u)\n",
				       field1, dsl1, field2, dsl2);

			min[field1*count+i[field1]] = dsl1;
			max[field1*count+i[field1]] = dsl2;
			if (++i[field1] >= count)
				break;
		}
		if (i[field1] >= count)
			break;
	}
}

static void poll_dsl_framecount_gen3(int pipe, uint32_t *min, uint32_t *max, const int count)
{
	uint32_t dsl, dsl1, dsl2, frm, frm1, frm2;
	bool field1, field2;
	int i[2] = {};

	switch (pipe) {
	case 0:
		frm = PIPEAFRAMEPIXEL;
		break;
	case 1:
		frm = PIPEBFRAMEPIXEL;
		break;
	default:
		return;
	}

	dsl = dsl_reg(pipe);

	while (!quit) {
		dsl1 = read_reg(dsl);
		frm1 = read_reg(frm) >> 24;
		frm2 = read_reg(frm) >> 24;
		dsl2 = read_reg(dsl);

		field1 = dsl1 & 0x80000000;
		field2 = dsl2 & 0x80000000;
		dsl1 &= ~0x80000000;
		dsl2 &= ~0x80000000;

		if (frm1 + 1 != frm2)
			continue;

		if (field1 != field2)
			printf("fields are different (%u:%u -> %u:%u)\n",
			       field1, dsl1, field2, dsl2);

		min[field1*count+i[field1]] = dsl1;
		max[field1*count+i[field1]] = dsl2;
		if (++i[field1] >= count)
			break;
	}
}

static void poll_dsl_pan(uint32_t devid, int pipe, int target_scanline, int target_fuzz,
			 uint32_t *min, uint32_t *max, const int count)
{
	uint32_t dsl, dsl1 = 0, dsl2 = 0;
	bool field1 = false, field2 = false;
	uint32_t saved, surf = 0;
	int i[2] = {};

	dsl = dsl_reg(pipe);
	surf = dspoffset_reg(devid, pipe);

	saved = read_reg(surf);

	while (!quit) {
		while (!quit) {
			dsl1 = read_reg(dsl);
			field1 = dsl1 & 0x80000000;
			dsl1 &= ~0x80000000;
			if (dsl1 == target_scanline)
				break;
		}

		write_reg(surf, saved+256);

		while (!quit) {
			dsl2 = read_reg(dsl);
			field2 = dsl1 & 0x80000000;
			dsl2 &= ~0x80000000;
			if (dsl2 == target_scanline + target_fuzz)
				break;
		}

		write_reg(surf, saved);

		if (field1 != field2)
			printf("fields are different (%u:%u -> %u:%u)\n",
			       field1, dsl1, field2, dsl2);

		min[field1*count+i[field1]] = dsl1;
		max[field1*count+i[field1]] = dsl2;
		if (++i[field1] >= count)
			break;
	}

	write_reg(surf, saved);
}

static void poll_dsl_flip(uint32_t devid, int pipe, int target_scanline, int target_fuzz,
			  uint32_t *min, uint32_t *max, const int count)
{
	uint32_t dsl, dsl1 = 0, dsl2 = 0;
	bool field1 = false, field2 = false;
	uint32_t saved, surf = 0;
	int i[2] = {};

	dsl = dsl_reg(pipe);
	surf = dspsurf_reg(devid, pipe);

	saved = read_reg(surf);

	while (!quit) {
		while (!quit) {
			dsl1 = read_reg(dsl);
			field1 = dsl1 & 0x80000000;
			dsl1 &= ~0x80000000;
			if (dsl1 == target_scanline)
				break;
		}

		write_reg(surf, saved+4096);

		while (!quit) {
			dsl2 = read_reg(dsl);
			field2 = dsl1 & 0x80000000;
			dsl2 &= ~0x80000000;
			if (dsl2 == target_scanline + target_fuzz)
				break;
		}

		write_reg(surf, saved);

		if (field1 != field2)
			printf("fields are different (%u:%u -> %u:%u)\n",
			       field1, dsl1, field2, dsl2);

		min[field1*count+i[field1]] = dsl1;
		max[field1*count+i[field1]] = dsl2;
		if (++i[field1] >= count)
			break;
	}

	write_reg(surf, saved);
}

static void poll_dsl_surflive(uint32_t devid, int pipe,
			      uint32_t *min, uint32_t *max, const int count)
{
	uint32_t dsl, dsl1 = 0, dsl2 = 0, surf, surf1, surf2, surflive, surfl1 = 0, surfl2, saved, tmp;
	bool field1 = false, field2 = false;
	int i[2] = {};

	switch (pipe) {
	case 0:
		surflive = DSPASURFLIVE;
		break;
	case 1:
		surflive = DSPBSURFLIVE;
		break;
	case 2:
		surflive = DSPCSURFLIVE;
		break;
	default:
		return;
	}

	dsl = dsl_reg(pipe);
	surf = dspsurf_reg(devid, pipe);

	saved = read_reg(surf);

	surf1 = saved & ~0xfff;
	surf2 = surf1 + 4096;

	while (!quit) {
		write_reg(surf, surf2);

		while (!quit) {
			dsl1 = read_reg(dsl);
			surfl1 = read_reg(surflive) & ~0xfff;
			surfl2 = read_reg(surflive) & ~0xfff;
			dsl2 = read_reg(dsl);

			field1 = dsl1 & 0x80000000;
			field2 = dsl2 & 0x80000000;
			dsl1 &= ~0x80000000;
			dsl2 &= ~0x80000000;

			if (surfl2 == surf2)
				break;
		}

		if (surfl1 != surf2) {
			if (field1 != field2)
				printf("fields are different (%u:%u -> %u:%u)\n",
				       field1, dsl1, field2, dsl2);

			min[field1*count+i[field1]] = dsl1;
			max[field1*count+i[field1]] = dsl2;
			if (++i[field1] >= count)
				break;
		}

		tmp = surf1;
		surf1 = surf2;
		surf2 = tmp;
	}

	write_reg(surf, saved);
}

static void poll_dsl_wrap(int pipe, uint32_t *min, uint32_t *max, const int count)
{
	uint32_t dsl, dsl1, dsl2;
	bool field1, field2;
	int i[2] = {};

	dsl = dsl_reg(pipe);

	while (!quit) {
		dsl1 = read_reg(dsl);
		dsl2 = read_reg(dsl);

		field1 = dsl1 & 0x80000000;
		field2 = dsl2 & 0x80000000;
		dsl1 &= ~0x80000000;
		dsl2 &= ~0x80000000;

		if (dsl2 >= dsl1)
			continue;

		if (field1 != field2)
			printf("fields are different (%u:%u -> %u:%u)\n",
			       field1, dsl1, field2, dsl2);

		min[field1*count+i[field1]] = dsl1;
		max[field1*count+i[field1]] = dsl2;
		if (++i[field1] >= count)
			break;
	}
}

static void poll_dsl_field(int pipe, uint32_t *min, uint32_t *max, const int count)
{
	uint32_t dsl, dsl1, dsl2;
	bool field1, field2;
	int i[2] = {};

	dsl = dsl_reg(pipe);

	while (!quit) {
		dsl1 = read_reg(dsl);
		dsl2 = read_reg(dsl);

		field1 = dsl1 & 0x80000000;
		field2 = dsl2 & 0x80000000;
		dsl1 &= ~0x80000000;
		dsl2 &= ~0x80000000;

		if (field1 == field2)
			continue;

		min[field1*count+i[field1]] = dsl1;
		max[field1*count+i[field1]] = dsl2;
		if (++i[field1] >= count)
			break;
	}
}

static char pipe_name(int pipe)
{
	return pipe + 'A';
}

static const char *test_name(enum test test, int pipe, int bit, bool test_pixel_count)
{
	static char str[32];
	const char *type = test_pixel_count ? "pixel" : "dsl";

	switch (test) {
	case TEST_PIPESTAT:
		snprintf(str, sizeof str, "%s / pipe %c / PIPESTAT[%d] (gmch)", type, pipe_name(pipe), bit);
		return str;
	case TEST_IIR_GEN2:
		snprintf(str, sizeof str, "%s / pipe %c / IIR[%d] (gen2)", type, pipe_name(pipe), bit);
		return str;
	case TEST_IIR_GEN3:
		snprintf(str, sizeof str, "%s / pipe %c / IIR[%d] (gen3+)", type, pipe_name(pipe), bit);
		return str;
	case TEST_DEIIR:
		snprintf(str, sizeof str, "%s / pipe %c / DEIIR[%d] (pch)", type, pipe_name(pipe), bit);
		return str;
	case TEST_FRAMECOUNT_GEN3:
		snprintf(str, sizeof str, "%s / pipe %c / Frame count (gen3/4)", type, pipe_name(pipe));
		return str;
	case TEST_FRAMECOUNT_G4X:
		snprintf(str, sizeof str, "%s / pipe %c / Frame count (g4x+)", type, pipe_name(pipe));
		return str;
	case TEST_FLIPCOUNT:
		snprintf(str, sizeof str, "%s / pipe %c / Flip count (g4x+)", type, pipe_name(pipe));
		return str;
	case TEST_PAN:
		snprintf(str, sizeof str, "%s / pipe %c / Pan", type, pipe_name(pipe));
		return str;
	case TEST_FLIP:
		snprintf(str, sizeof str, "%s / pipe %c / Flip", type, pipe_name(pipe));
		return str;
	case TEST_SURFLIVE:
		snprintf(str, sizeof str, "%s / pipe %c / Surflive", type, pipe_name(pipe));
		return str;
	case TEST_WRAP:
		snprintf(str, sizeof str, "%s / pipe %c / Wrap", type, pipe_name(pipe));
		return str;
	case TEST_FIELD:
		snprintf(str, sizeof str, "%s / pipe %c / Field", type, pipe_name(pipe));
		return str;
	default:
		return "";
	}
}

static void usage(const char *name)
{
	fprintf(stderr, "Usage: %s [options]\n"
		" -t,--test <pipestat|iir|framecount|flipcount|pan|flip|surflive|wrap|field>\n"
		" -p,--pipe <pipe>\n"
		" -b,--bit <bit>\n"
		" -l,--line <target scanline/pixel>\n"
		" -f,--fuzz <target fuzz>\n"
		" -x,--pixel\n",
		name);
	exit(1);
}

int main(int argc, char *argv[])
{
	int fd, i;
	int pipe = 0, bit = 0, target_scanline = 0, target_fuzz = 1;
	bool test_pixelcount = false;
	uint32_t devid;
	uint32_t min[2*128] = {};
	uint32_t max[2*128] = {};
	uint32_t a, b;
	enum test test = TEST_INVALID;
	const int count = ARRAY_SIZE(min)/2;

	for (;;) {
		static const struct option long_options[] = {
			{ .name = "test", .has_arg = required_argument, },
			{ .name = "pipe", .has_arg = required_argument, },
			{ .name = "bit", .has_arg = required_argument, },
			{ .name = "line", .has_arg = required_argument, },
			{ .name = "fuzz", .has_arg = required_argument, },
			{ .name = "pixel", .has_arg = no_argument, },
			{ },
		};

		int opt = getopt_long(argc, argv, "t:p:b:l:f:x", long_options, NULL);
		if (opt == -1)
			break;

		switch (opt) {
		case 't':
			if (!strcmp(optarg, "pipestat"))
				test = TEST_PIPESTAT;
			else if (!strcmp(optarg, "iir"))
				test = TEST_IIR;
			else if (!strcmp(optarg, "framecount"))
				test = TEST_FRAMECOUNT;
			else if (!strcmp(optarg, "flipcount"))
				test = TEST_FLIPCOUNT;
			else if (!strcmp(optarg, "pan"))
				test = TEST_PAN;
			else if (!strcmp(optarg, "flip"))
				test = TEST_FLIP;
			else if (!strcmp(optarg, "surflive"))
				test = TEST_SURFLIVE;
			else if (!strcmp(optarg, "wrap"))
				test = TEST_WRAP;
			else if (!strcmp(optarg, "field"))
				test = TEST_FIELD;
			else
				usage(argv[0]);
			break;
		case 'p':
			pipe = atoi(optarg);
			if (pipe < 0 || pipe > 2)
				usage(argv[0]);
			break;
		case 'b':
			bit = atoi(optarg);
			if (bit < 0 || bit > 31)
				usage(argv[0]);
			break;
		case 'l':
			target_scanline = atoi(optarg);
			if (target_scanline < 0)
				usage(argv[0]);
			break;
		case 'f':
			target_fuzz = atoi(optarg);
			if (target_fuzz <= 0)
				usage(argv[0]);
			break;
		case 'x':
			test_pixelcount = true;
			break;
		}
	}

	fd = drm_open_any();
	devid = intel_get_drm_devid(fd);
	close(fd);

	/*
	 * check if the requires registers are
	 * avilable on the current platform.
	 */
	if (IS_GEN2(devid)) {
		if (pipe > 1)
			usage(argv[0]);

		if (test_pixelcount)
			usage(argv[0]);

		switch (test) {
		case TEST_IIR:
			test = TEST_IIR_GEN2;
			break;
		case TEST_PIPESTAT:
		case TEST_PAN:
			break;
		case TEST_FLIP:
			test = TEST_PAN;
			break;
		default:
			usage(argv[0]);
		}
	} else if (IS_GEN3(devid) ||
		   (IS_GEN4(devid) && !IS_G4X(devid))) {
		if (pipe > 1)
			usage(argv[0]);

		switch (test) {
		case TEST_IIR:
			test = TEST_IIR_GEN3;
			break;
		case TEST_FRAMECOUNT:
			test = TEST_FRAMECOUNT_GEN3;
			break;
		case TEST_PIPESTAT:
		case TEST_PAN:
		case TEST_WRAP:
		case TEST_FIELD:
			break;
		case TEST_FLIP:
			if (IS_GEN3(devid))
				test = TEST_PAN;
			break;
		default:
			usage(argv[0]);
		}
	} else if (IS_G4X(devid) || IS_VALLEYVIEW(devid)) {
		if (IS_VALLEYVIEW(devid))
			vlv_offset = 0x180000;

		if (pipe > 1)
			usage(argv[0]);

		if (test_pixelcount)
			usage(argv[0]);

		switch (test) {
		case TEST_IIR:
			test = TEST_IIR_GEN3;
			break;
		case TEST_FRAMECOUNT:
			test = TEST_FRAMECOUNT_G4X;
			break;
		case TEST_FLIPCOUNT:
		case TEST_PIPESTAT:
		case TEST_PAN:
		case TEST_FLIP:
		case TEST_SURFLIVE:
		case TEST_WRAP:
		case TEST_FIELD:
			break;
		default:
			usage(argv[0]);
		}
	} else if (HAS_PCH_SPLIT(devid) &&
		   (IS_GEN5(devid) || IS_GEN6(devid) || IS_GEN7(devid))) {
		if (pipe > 1 &&
		    (IS_GEN5(devid) || IS_GEN6(devid)))
			usage(argv[0]);

		if (test_pixelcount)
			usage(argv[0]);

		switch (test) {
		case TEST_IIR:
			test = TEST_DEIIR;
			break;
		case TEST_FRAMECOUNT:
			test = TEST_FRAMECOUNT_G4X;
			break;
		case TEST_FLIPCOUNT:
		case TEST_PAN:
		case TEST_FLIP:
		case TEST_SURFLIVE:
		case TEST_WRAP:
		case TEST_FIELD:
			break;
		default:
			usage(argv[0]);
		}
	} else if (IS_GEN8(devid)) {
		if (test_pixelcount)
			usage(argv[0]);

		switch (test) {
		case TEST_IIR:
			test = TEST_DEIIR;
			break;
		case TEST_FRAMECOUNT:
			test = TEST_FRAMECOUNT_G4X;
			break;
		case TEST_FLIPCOUNT:
		case TEST_PAN:
		case TEST_FLIP:
		case TEST_SURFLIVE:
		case TEST_WRAP:
		case TEST_FIELD:
			break;
		default:
			usage(argv[0]);
		}
	} else {
		usage(argv[0]);
	}

	switch (test) {
	case TEST_IIR:
	case TEST_FRAMECOUNT:
		/* should no longer have the generic tests here */
		assert(0);
	default:
		break;
	}

	intel_register_access_init(intel_get_pci_device(), 0);

	printf("%s?\n", test_name(test, pipe, bit, test_pixelcount));

	signal(SIGHUP, sighandler);
	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);

	switch (test) {
	case TEST_PIPESTAT:
		if (test_pixelcount)
			poll_pixel_pipestat(pipe, bit, min, max, count);
		else
			poll_dsl_pipestat(pipe, bit, min, max, count);
		break;
	case TEST_IIR_GEN2:
		assert(!test_pixelcount);
		poll_dsl_iir_gen2(pipe, bit, min, max, count);
		break;
	case TEST_IIR_GEN3:
		if (test_pixelcount)
			poll_pixel_iir_gen3(pipe, bit, min, max, count);
		else
			poll_dsl_iir_gen3(pipe, bit, min, max, count);
		break;
	case TEST_DEIIR:
		assert(!test_pixelcount);
		poll_dsl_deiir(devid, pipe, bit, min, max, count);
		break;
	case TEST_FRAMECOUNT_GEN3:
		if (test_pixelcount)
			poll_pixel_framecount_gen3(pipe, min, max, count);
		else
			poll_dsl_framecount_gen3(pipe, min, max, count);
		break;
	case TEST_FRAMECOUNT_G4X:
		assert(!test_pixelcount);
		poll_dsl_framecount_g4x(pipe, min, max, count);
		break;
	case TEST_FLIPCOUNT:
		assert(!test_pixelcount);
		poll_dsl_flipcount_g4x(devid, pipe, min, max, count);
		break;
	case TEST_PAN:
		if (test_pixelcount)
			poll_pixel_pan(devid, pipe, target_scanline, target_fuzz,
				       min, max, count);
		else
			poll_dsl_pan(devid, pipe, target_scanline, target_fuzz,
				     min, max, count);
		break;
	case TEST_FLIP:
		if (test_pixelcount)
			poll_pixel_flip(devid, pipe, target_scanline, target_fuzz,
					min, max, count);
		else
			poll_dsl_flip(devid, pipe, target_scanline, target_fuzz,
				      min, max, count);
		break;
	case TEST_SURFLIVE:
		poll_dsl_surflive(devid, pipe, min, max, count);
		break;
	case TEST_WRAP:
		if (test_pixelcount)
			poll_pixel_wrap(pipe, min, max, count);
		else
			poll_dsl_wrap(pipe, min, max, count);
		break;
	case TEST_FIELD:
		poll_dsl_field(pipe, min, max, count);
		break;
	default:
		assert(0);
	}

	intel_register_access_fini();

	if (quit)
		return 0;

	for (i = 0; i < count; i++) {
		if (min[0*count+i] == 0 && max[0*count+i] == 0)
			break;
		printf("[%u] %4u - %4u (%4u)\n", 0, min[0*count+i], max[0*count+i],
		       (min[0*count+i] + max[0*count+i] + 1) >> 1);
	}
	for (i = 0; i < count; i++) {
		if (min[1*count+i] == 0 && max[1*count+i] == 0)
			break;
		printf("[%u] %4u - %4u (%4u)\n", 1, min[1*count+i], max[1*count+i],
		       (min[1*count+i] + max[1*count+i] + 1) >> 1);
	}

	a = 0;
	b = 0xffffffff;
	for (i = 0; i < count; i++) {
		if (min[0*count+i] == 0 && max[0*count+i] == 0)
			break;
		a = max(a, min[0*count+i]);
		b = min(b, max[0*count+i]);
	}

	printf("%s: [%u] %6u - %6u\n", test_name(test, pipe, bit, test_pixelcount), 0, a, b);

	a = 0;
	b = 0xffffffff;
	for (i = 0; i < count; i++) {
		if (min[1*count+i] == 0 && max[1*count+i] == 0)
			break;
		a = max(a, min[1*count+i]);
		b = min(b, max[1*count+i]);
	}

	printf("%s: [%u] %6u - %6u\n", test_name(test, pipe, bit, test_pixelcount), 1, a, b);

	return 0;
}
