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
 */

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member) ({			\
	typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})

#define div_u64(a, b)	((a) / (b))

/*
 * Stub a few defines/structures
 */

#define I915_MAX_PIPES	3
#define I915_MAX_PLANES	3
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define for_each_pipe(p) for ((p) = 0; (p) < 3; (p)++)
#define for_each_plane(pipe, p) for ((p) = 0; (p) < 3; (p)++)

#define for_each_crtc(dev, crtc) \
	for (int i = 0; i < 3 && (crtc = &crtcs[i].base); i++)

#define for_each_intel_crtc(dev, intel_crtc) \
	for (int i = 0; i < 3, intel_crtc = &crtcs[i]; i++)

enum pipe {
	PIPE_A,
	PIPE_B,
	PIPE_C,
};

enum plane {
	PLANE_1,
	PLANE_2,
	PLANE_3,
};

#define pipe_name(p) ((p) + 'A')

struct drm_device {
	void *dev_private;
};

struct drm_i915_private {
	struct drm_device *dev;
};

struct drm_crtc {
	struct drm_device *dev;
	bool active;
};

static bool intel_crtc_active(struct drm_crtc *crtc)
{
	return crtc->active;
}

struct intel_crtc {
	struct drm_crtc base;
	enum pipe pipe;
};

static int intel_num_planes(struct intel_crtc *crtc)
{
	return 3;
}

struct intel_crtc crtcs[I915_MAX_PIPES];

#define to_intel_crtc(x) container_of(x, struct intel_crtc, base)

/*
 * DDB code
 */

struct intel_wm_config {
	unsigned int num_pipes_active;
};

struct intel_plane_wm_parameters {
	uint32_t horiz_pixels;
	uint32_t vert_pixels;
	uint8_t bytes_per_pixel;
	bool enabled;
	bool scaled;
};

struct skl_pipe_wm_parameters {
	bool active;
	uint32_t pipe_htotal;
	uint32_t pixel_rate; /* in KHz */
	struct intel_plane_wm_parameters plane[I915_MAX_PLANES];
	struct intel_plane_wm_parameters cursor;
};

struct skl_ddb_entry {
	uint16_t start, end;	/* in number of blocks. 'end' is exclusive */
};

static inline uint16_t skl_ddb_entry_size(const struct skl_ddb_entry *entry)
{
	/* end not set, clearly no allocation here. start can be 0 though */
	if (entry->end == 0)
		return 0;

	return entry->end - entry->start;
}

static inline bool skl_ddb_entry_equal(const struct skl_ddb_entry *e1,
				       const struct skl_ddb_entry *e2)
{
	if (e1->start == e2->start && e1->end == e2->end)
		return true;

	return false;
}

struct skl_ddb_allocation {
	struct skl_ddb_entry plane[I915_MAX_PIPES][I915_MAX_PLANES];
	struct skl_ddb_entry cursor[I915_MAX_PIPES];
};

/*
 * On gen9, we need to allocate Display Data Buffer (DDB) portions to the
 * different active planes.
 */

#define SKL_DDB_SIZE		896	/* in blocks */

static void
skl_ddb_get_pipe_allocation_limits(struct drm_device *dev,
				   struct drm_crtc *for_crtc,
				   const struct intel_wm_config *config,
				   const struct skl_pipe_wm_parameters *params,
				   struct skl_ddb_entry *alloc /* out */)
{
	struct drm_crtc *crtc;
	unsigned int pipe_size, ddb_size;
	int nth_active_pipe;

	if (!params->active) {
		alloc->start = 0;
		alloc->end = 0;
		return;
	}

	ddb_size = SKL_DDB_SIZE;
	ddb_size -= 4; /* 4 blocks for bypass path allocation */

	nth_active_pipe = 0;
	for_each_crtc(dev, crtc) {
		if (!intel_crtc_active(crtc))
			continue;

		if (crtc == for_crtc)
			break;

		nth_active_pipe++;
	}

	pipe_size = ddb_size / config->num_pipes_active;
	alloc->start = nth_active_pipe * ddb_size / config->num_pipes_active;
	alloc->end = alloc->start + pipe_size;
}

static unsigned int skl_cursor_allocation(const struct intel_wm_config *config)
{
	if (config->num_pipes_active == 1)
		return 32;

	return 8;
}

static unsigned int
skl_plane_relative_data_rate(const struct intel_plane_wm_parameters *p)
{
	return p->horiz_pixels * p->vert_pixels * p->bytes_per_pixel;
}

/*
 * We don't overflow 32 bits. Worst case is 3 planes enabled, each fetching
 * a 8192x4096@32bpp framebuffer:
 *   3 * 4096 * 8192  * 4 < 2^32
 */
static unsigned int
skl_get_total_relative_data_rate(struct intel_crtc *intel_crtc,
				 const struct skl_pipe_wm_parameters *params)
{
	unsigned int total_data_rate = 0;
	int plane;

	for (plane = 0; plane < intel_num_planes(intel_crtc); plane++) {
		const struct intel_plane_wm_parameters *p;

		p = &params->plane[plane];
		if (!p->enabled)
			continue;

		total_data_rate += skl_plane_relative_data_rate(p);
	}

	return total_data_rate;
}

static void
skl_allocate_pipe_ddb(struct drm_crtc *crtc,
		      const struct intel_wm_config *config,
		      const struct skl_pipe_wm_parameters *params,
		      struct skl_ddb_allocation *ddb /* out */)
{
	struct drm_device *dev = crtc->dev;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	enum pipe pipe = intel_crtc->pipe;
	struct skl_ddb_entry alloc;
	uint16_t alloc_size, start, cursor_blocks;
	uint16_t minimum[I915_MAX_PLANES];
	unsigned int total_data_rate;
	int plane;

	skl_ddb_get_pipe_allocation_limits(dev, crtc, config, params, &alloc);
	alloc_size = skl_ddb_entry_size(&alloc);
	if (alloc_size == 0) {
		memset(ddb->plane[pipe], 0, sizeof(ddb->plane[pipe]));
		memset(&ddb->cursor[pipe], 0, sizeof(ddb->cursor[pipe]));
		return;
	}

	cursor_blocks = skl_cursor_allocation(config);
	ddb->cursor[pipe].start = alloc.end - cursor_blocks;
	ddb->cursor[pipe].end = alloc.end;

	alloc_size -= cursor_blocks;
	alloc.end -= cursor_blocks;

	/* 1. Allocate the mininum required blocks for each active plane */
	for_each_plane(pipe, plane) {
		const struct intel_plane_wm_parameters *p;

		p = &params->plane[plane];
		if (!p->enabled)
			continue;

		minimum[plane] = 8;
		alloc_size -= minimum[plane];
	}

	/*
	 * 2. Distribute the remaining space in proportion to the amount of
	 * data each plane needs to fetch from memory.
	 *
	 * FIXME: we may not allocate every single block here.
	 */
	total_data_rate = skl_get_total_relative_data_rate(intel_crtc, params);

	start = alloc.start;
	for (plane = 0; plane < intel_num_planes(intel_crtc); plane++) {
		const struct intel_plane_wm_parameters *p;
		unsigned int data_rate;
		uint16_t plane_blocks;

		p = &params->plane[plane];
		if (!p->enabled)
			continue;

		data_rate = skl_plane_relative_data_rate(p);

		/*
		 * promote the expression to 64 bits to avoid overflowing, the
		 * result is < available as data_rate / total_data_rate < 1
		 */
		plane_blocks = minimum[plane];
		plane_blocks += div_u64((uint64_t)alloc_size * data_rate,
					total_data_rate);

		ddb->plane[pipe][plane].start = start;
		ddb->plane[pipe][plane].end = start + plane_blocks;

		start += plane_blocks;
	}

}

static void skl_ddb_check_entry(struct skl_ddb_entry *entry, int16_t *cursor)
{

	if (skl_ddb_entry_size(entry) == 0)
		return;

	/* check that ->start is the next available block */
	if (entry->start < *cursor)
		printf("error: allocation overlaps previous block\n");
	else if (entry->start >= *cursor + 1)
		printf("warning: allocation leaves a hole\n");

	*cursor = entry->end;
}

static void skl_ddb_check_last_allocation(int16_t cursor)
{
	uint16_t last_offset = SKL_DDB_SIZE - 4;

	if (cursor < last_offset)
		printf("warning: %d blocks not allocated\n",
		       last_offset - cursor);
	else if (cursor > last_offset)
		printf("error: allocation greater than available space\n");
}

static void skl_ddb_print(struct skl_ddb_allocation *ddb)
{
	struct skl_ddb_entry *entry;
	enum pipe pipe;
	int plane;
	int16_t cursor = 0;

	printf("%-15s%8s%8s%8s\n", "", "Start", "End", "Size");

	for_each_pipe(pipe) {
		printf("Pipe %c\n", pipe_name(pipe));

		for_each_plane(pipe, plane) {
			entry = &ddb->plane[pipe][plane];

			printf("  Plane%-8d%8u%8u%8u\n", plane + 1,
			       entry->start, entry->end,
			       skl_ddb_entry_size(entry));

			skl_ddb_check_entry(entry, &cursor);
		}

		entry = &ddb->cursor[pipe];
		printf("  %-13s%8u%8u%8u\n", "Cursor", entry->start,
		       entry->end, skl_ddb_entry_size(entry));

		skl_ddb_check_entry(entry, &cursor);
	}

	skl_ddb_check_last_allocation(cursor);
}

static struct drm_device drm_device;
static struct drm_i915_private drm_i915_private;

static void init_stub(void)
{
	int i;

	drm_device.dev_private = &drm_i915_private;
	drm_i915_private.dev = &drm_device;

	for (i = 0; i < I915_MAX_PIPES; i++) {
		crtcs[i].base.dev = &drm_device;
		crtcs[i].pipe = i;
	}
}

struct wm_input {
	struct intel_wm_config config;
	struct skl_pipe_wm_parameters params[I915_MAX_PIPES];
};

static void wm_input_reset(struct wm_input *in)
{
	memset(in, 0, sizeof(*in));
}

static void wm_enable_plane(struct wm_input *in,
			    enum pipe pipe, enum plane plane,
			    uint32_t width, uint32_t height, int bpp)
{
	enum pipe i;

	in->params[pipe].active = 1;

	in->config.num_pipes_active = 0;
	for_each_pipe(i)
		if (in->params[i].active)
			in->config.num_pipes_active++;

	in->params[pipe].plane[plane].horiz_pixels = width;
	in->params[pipe].plane[plane].vert_pixels = height;
	in->params[pipe].plane[plane].bytes_per_pixel = bpp;
	in->params[pipe].plane[plane].enabled = true;
}

static void skl_ddb_allocate(struct wm_input *in,
			     struct skl_ddb_allocation *out)
{
	struct drm_crtc *crtc;

	for_each_crtc(, crtc) {
		enum pipe pipe = to_intel_crtc(crtc)->pipe;

		skl_allocate_pipe_ddb(crtc,
				      &in->config, &in->params[pipe], out);
	}
}

int main(int argc, char **argv)
{
	struct wm_input in;
	static struct skl_ddb_allocation ddb;

	init_stub();

	wm_input_reset(&in);
	wm_enable_plane(&in, PIPE_A, PLANE_1, 1280, 1024, 4);
	wm_enable_plane(&in, PIPE_A, PLANE_2,  100,  100, 4);
	skl_ddb_allocate(&in, &ddb);
	skl_ddb_print(&ddb);

	return 0;
}
