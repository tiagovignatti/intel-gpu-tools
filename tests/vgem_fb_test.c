/*
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This is a test meant to benchmark VGEM performance. There are three running
 * paths (VGEM, GEM and DRM) that one should set via the define macros below.
 * At the moment we are only measuring memory mapping performance of a PRIME
 * imported buffer object - this is pretty much useful for graphics systems
 * like the Chrome's where one process creates the buffer to render and another
 * maps to display it on the screen.
 *
 * vgem_fb_test is originally inspired from:
 * https://chromium.googlesource.com/chromiumos/platform/drm-tests/+/master/vgem_fb_test.c
 */

#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "drmtest.h"

// to use DRM, one has to comment USE_GEM defines.
//#define USE_GEM

#ifdef USE_GEM
#include "ioctl_wrappers.h"
#else
#include <gbm.h>
#endif

#define BUFFERS 2

struct context {
	int drm_card_fd;
	int card_fd;

	drmModeRes *resources;
	drmModeConnector *connector;
	drmModeEncoder *encoder;
	drmModeModeInfo *mode;

	uint32_t drm_fb_id[BUFFERS];
#ifdef USE_GEM
	drm_intel_bufmgr *bufmgr;

	// render
	drm_intel_bo *intel_bo[BUFFERS];
	unsigned long bo_stride;
#else
	struct gbm_device *drm_gbm;

	// render
	struct gbm_bo *drm_bo[BUFFERS];
#endif
};

#ifdef USE_GEM
#define BO_SIZE (16*4096)
#endif

static int disable_profiling = false;

static void disable_psr(void) {
	const char psr_path[] = "/sys/module/i915/parameters/enable_psr";
	int psr_fd = open(psr_path, O_WRONLY);

	if (psr_fd < 0)
		return;

	if (write(psr_fd, "0", 1) == -1) {
		fprintf(stderr, "failed to disable psr\n");
	} else {
		fprintf(stderr, "disabled psr\n");
	}

	close(psr_fd);
}

static void do_fixes(void) {
	disable_psr();
}

const char *drm_card_path = "/dev/dri/card0";

static double elapsed(const struct timeval *start, const struct timeval *end)
{
	return 1e6*(end->tv_sec - start->tv_sec) + (end->tv_usec - start->tv_usec);
}

#ifdef USE_GEM
static void * mmap_intel_bo(drm_intel_bo *buffer)
{
	if (drm_intel_bo_map(buffer, 1)) {
		fprintf(stderr, "fail to map a drm buffer\n");
		return NULL;
	}
	assert(buffer->virtual);
	return buffer->virtual;
}
#endif

static bool setup_drm(struct context *ctx)
{
	int fd = ctx->drm_card_fd;
	drmModeRes *resources = NULL;
	drmModeConnector *connector = NULL;
	drmModeEncoder *encoder = NULL;
	int i, j;

	resources = drmModeGetResources(fd);
	if (!resources) {
		fprintf(stderr, "drmModeGetResources failed\n");
		return false;
	}

	for (i = 0; i < resources->count_connectors; i++) {
		connector = drmModeGetConnector(fd, resources->connectors[i]);
		if (connector == NULL)
			continue;

		if (connector->connection == DRM_MODE_CONNECTED &&
				connector->count_modes > 0)
			break;

		drmModeFreeConnector(connector);
	}

	if (i == resources->count_connectors) {
		fprintf(stderr, "no currently active connector found\n");
		drmModeFreeResources(resources);
		return false;
	}

	for (i = 0; i < resources->count_encoders; i++) {
		encoder = drmModeGetEncoder(fd, resources->encoders[i]);

		if (encoder == NULL)
			continue;

		for (j = 0; j < connector->count_encoders; j++) {
			if (encoder->encoder_id == connector->encoders[j])
				break;
		}

		if (j == connector->count_encoders) {
			drmModeFreeEncoder(encoder);
			continue;
		}

		break;
	}

	if (i == resources->count_encoders) {
		fprintf(stderr, "no supported encoder found\n");
		drmModeFreeConnector(connector);
		drmModeFreeResources(resources);
		return false;
	}

	for (i = 0; i < resources->count_crtcs; i++) {
		if (encoder->possible_crtcs & (1 << i)) {
			encoder->crtc_id = resources->crtcs[i];
			break;
		}
	}

	if (i == resources->count_crtcs) {
		fprintf(stderr, "no possible crtc found\n");
		drmModeFreeEncoder(encoder);
		drmModeFreeConnector(connector);
		drmModeFreeResources(resources);
		return false;
	}

	ctx->resources = resources;
	ctx->connector = connector;
	ctx->encoder = encoder;
	ctx->mode = &connector->modes[0];

	return true;
}

#define STEP_SKIP 0
#define STEP_MMAP 1
#define STEP_FAULT 2
#define STEP_FLIP 3
#define STEP_DRAW 4

static void show_sequence(const int *sequence)
{
	int sequence_subindex;
	fprintf(stderr, "starting sequence: ");
	for (sequence_subindex = 0; sequence_subindex < 4; sequence_subindex++) {
		switch (sequence[sequence_subindex]) {
		case STEP_SKIP:
			break;
		case STEP_MMAP:
			fprintf(stderr, "mmap ");
			break;
		case STEP_FAULT:
			fprintf(stderr, "fault ");
			break;
		case STEP_FLIP:
			fprintf(stderr, "flip ");
			break;
		case STEP_DRAW:
			fprintf(stderr, "draw ");
			break;
		default:
			fprintf(stderr, "<unknown step %d> (aborting!)\n", sequence[sequence_subindex]);
			abort();
			break;
		}
	}
	fprintf(stderr, "\n");
}

static void draw(struct context *ctx)
{
	int i;

	// Run the drawing routine with the key driver events in different
	// sequences.
	const int sequences[4][4] = {
		{ STEP_MMAP, STEP_FAULT, STEP_FLIP, STEP_DRAW },
		{ STEP_MMAP, STEP_FLIP,  STEP_DRAW, STEP_SKIP },
		{ STEP_MMAP, STEP_DRAW,  STEP_FLIP, STEP_SKIP },
		{ STEP_FLIP, STEP_MMAP,  STEP_DRAW, STEP_SKIP },
	};

	int sequence_index = 0;
	int sequence_subindex = 0;

	int fb_idx = 1;

	for (sequence_index = 0; sequence_index < 4; sequence_index++) {
		show_sequence(sequences[sequence_index]);
		for (i = 0; i < 0x100; i++) {
#ifdef USE_GEM
			size_t bo_stride = ctx->bo_stride;
			size_t bo_size = ctx->bo_stride * ctx->mode->vdisplay;
#else
			size_t bo_stride = gbm_bo_get_stride(ctx->drm_bo[fb_idx]);
			size_t bo_size = gbm_bo_get_stride(ctx->drm_bo[fb_idx]) * gbm_bo_get_height(ctx->drm_bo[fb_idx]);
#endif
			uint32_t *bo_ptr;
			volatile uint32_t *ptr;
			struct timeval start, end;

			for (sequence_subindex = 0; sequence_subindex < 4; sequence_subindex++) {
				switch (sequences[sequence_index][sequence_subindex]) {
				case STEP_MMAP:
					if (!disable_profiling)
						gettimeofday(&start, NULL);
#ifdef USE_GEM
					bo_ptr = (uint32_t*)mmap_intel_bo(ctx->intel_bo[fb_idx]);
#else
					bo_ptr = gbm_bo_map(ctx->drm_bo[fb_idx]);
#endif
					if (!disable_profiling) {
						gettimeofday(&end, NULL);
						fprintf(stderr, "time to execute mmap: %7.3fms\n",
								elapsed(&start, &end) / 1000);
					}
					ptr = bo_ptr;
					break;

				case STEP_FAULT:
					*ptr = 1234567;
					break;

				case STEP_FLIP:
					drmModePageFlip(ctx->drm_card_fd, ctx->encoder->crtc_id,
						ctx->drm_fb_id[fb_idx],
						0,
						NULL);
					break;

				case STEP_DRAW:
					for (ptr = bo_ptr; ptr < bo_ptr + (bo_size / sizeof(*bo_ptr)); ptr++) {
						int y = ((void*)ptr - (void*)bo_ptr) / bo_stride;
						int x = ((void*)ptr - (void*)bo_ptr - bo_stride * y) / sizeof(*ptr);
						x -= 100;
						y -= 100;
						*ptr = 0xff000000;
						if (x * x + y * y < i * i)
							*ptr |= (i % 0x100) << 8;
						else
							*ptr |= 0xff | (sequence_index * 64 << 16);
					}
					break;

				case STEP_SKIP:
				default:
					break;
				}
			}
#ifdef USE_GEM
			drm_intel_bo_unmap(ctx->intel_bo[fb_idx]);
#else
			gbm_bo_unmap(ctx->drm_bo[fb_idx]);
#endif
			usleep(1e6 / 120); /* 120 Hz */

			fb_idx = fb_idx ^ 1;
		}
	}
}

static int opt_handler(int opt, int opt_index, void *data)
{
  switch (opt) {
		case 'd':
			drm_card_path = optarg;
			break;
		case 'p':
			disable_profiling = true;
			break;
	}

	return 0;
}

int main(int argc, char **argv)
{
	int ret = 0;
	struct context ctx;
	unsigned long bo_stride = 0;
	uint32_t bo_handle[BUFFERS];
	int drm_prime_fd;
	size_t i;

	igt_simple_init_parse_opts(&argc, argv, "d:p", NULL, NULL, opt_handler, NULL);
	igt_skip_on_simulation();

	do_fixes();

	ctx.drm_card_fd = open(drm_card_path, O_RDWR);
	if (ctx.drm_card_fd < 0) {
		fprintf(stderr, "failed to open %s\n", drm_card_path);
		ret = 1;
		goto fail;
	}
#ifdef USE_GEM
	ctx.bufmgr = drm_intel_bufmgr_gem_init(ctx.drm_card_fd, BO_SIZE);
	ctx.card_fd = ctx.drm_card_fd;
	fprintf(stderr, "Method to open video card: GEM\n");
#else
	ctx.card_fd = ctx.drm_card_fd;
	fprintf(stderr, "Method to open video card: DRM\n");

	/* GBM will load a dri driver, but even though they need symbols from
	 * libglapi, in some version of Mesa they are not linked to it. Since
	 * only the gl-renderer module links to it, the call above won't make
	 * these symbols globally available, and loading the DRI driver fails.
	 * Workaround this by dlopen()'ing libglapi with RTLD_GLOBAL. */
	dlopen("libglapi.so.0", RTLD_LAZY | RTLD_GLOBAL);

	ctx.drm_gbm = gbm_create_device(ctx.drm_card_fd);
	if (!ctx.drm_gbm) {
		fprintf(stderr, "failed to create gbm device on %s\n", drm_card_path);
		ret = 1;
		goto close_card;
	}
#endif // USE_GEM

	if (!setup_drm(&ctx)) {
		fprintf(stderr, "failed to setup drm resources\n");
		ret = 1;
		goto destroy_drm_gbm;
	}

	fprintf(stderr, "display size: %dx%d\n",
		ctx.mode->hdisplay, ctx.mode->vdisplay);

	for (i = 0; i < BUFFERS; ++i) {
#ifdef USE_GEM
		uint32_t tiling_mode = I915_TILING_NONE;
		drm_intel_bo *intel_bo = NULL;
		intel_bo = drm_intel_bo_alloc_tiled(
		                             ctx.bufmgr,
		                             "chromium-gpu-memory-buffer",
		                             ctx.mode->hdisplay, ctx.mode->vdisplay,
		                             4, &tiling_mode, &bo_stride, 0);
		ctx.bo_stride = bo_stride;
		drm_intel_bo_gem_export_to_prime(intel_bo, &drm_prime_fd);
#else
		struct gbm_bo *gbm_buffer;
		gbm_buffer = gbm_bo_create(ctx.drm_gbm,
			ctx.mode->hdisplay, ctx.mode->vdisplay, GBM_BO_FORMAT_XRGB8888,
			GBM_BO_USE_LINEAR);

		if (!gbm_buffer) {
			fprintf(stderr, "failed to create buffer object\n");
			ret = 1;
			goto free_buffers;
		}

		bo_stride = gbm_bo_get_stride(gbm_buffer);

		drm_prime_fd = gbm_bo_get_fd(gbm_buffer);
#endif // USE_GEM
		if (drm_prime_fd < 0) {
			fprintf(stderr, "failed to turn handle into fd\n");
			ret = 1;
			goto free_buffers;
		}

		ret = drmPrimeFDToHandle(ctx.drm_card_fd, drm_prime_fd,
			                       &bo_handle[i]);
		if (ret) {
			fprintf(stderr, "failed to import handle\n");
			ret = 1;
			goto free_buffers;
		}
#ifdef USE_GEM
		ctx.intel_bo[i] = drm_intel_bo_gem_create_from_prime(
				ctx.bufmgr, drm_prime_fd, BO_SIZE);
#else
		struct gbm_import_fd_data gbm_dmabuf = {
				.fd     = drm_prime_fd,
				.width  = ctx.mode->hdisplay,
				.height = ctx.mode->vdisplay,
				.stride = bo_stride,
				.format = DRM_FORMAT_XRGB8888
		};

		ctx.drm_bo[i] = gbm_bo_import(ctx.drm_gbm, GBM_BO_IMPORT_FD, &gbm_dmabuf, 0);
#endif

		ret = drmModeAddFB(ctx.drm_card_fd, ctx.mode->hdisplay, ctx.mode->vdisplay,
			24, 32, bo_stride, bo_handle[i], &ctx.drm_fb_id[i]);
		if (ret) {
			fprintf(stderr, "failed to add fb\n");
			ret = 1;
			goto free_buffers;
		}
	}

	if (drmModeSetCrtc(ctx.drm_card_fd, ctx.encoder->crtc_id, ctx.drm_fb_id[0],
			0, 0, &ctx.connector->connector_id, 1, ctx.mode)) {
		fprintf(stderr, "failed to set CRTC\n");
		ret = 1;
		goto free_buffers;
	}

	draw(&ctx);

free_buffers:
	for (i = 0; i < BUFFERS; ++i) {
		if (ctx.drm_fb_id[i])
			drmModeRmFB(ctx.drm_card_fd, ctx.drm_fb_id[i]);
#ifndef USE_GEM
		if (ctx.drm_bo[i])
			gbm_bo_destroy(ctx.drm_bo[i]);
#endif
	}

	drmModeFreeConnector(ctx.connector);
	drmModeFreeEncoder(ctx.encoder);
	drmModeFreeResources(ctx.resources);
destroy_drm_gbm:
#ifndef USE_GEM
	gbm_device_destroy(ctx.drm_gbm);
#endif
close_card:
	close(ctx.drm_card_fd);
fail:
	igt_exit();
}
