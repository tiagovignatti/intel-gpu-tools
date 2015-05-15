/*
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <gbm.h>
#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define BUFFERS 2

struct context {
	int drm_card_fd;
	int vgem_card_fd;
	struct gbm_device *drm_gbm;

	drmModeRes *resources;
	drmModeConnector *connector;
	drmModeEncoder *encoder;
	drmModeModeInfo *mode;

	struct gbm_bo *gbm_buffer[BUFFERS];
	uint32_t vgem_bo_handle[BUFFERS];
	uint32_t drm_fb_id[BUFFERS];

};

void disable_psr() {
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

void do_fixes() {
	disable_psr();
}

const char g_sys_card_path_format[] =
   "/sys/bus/platform/devices/vgem/drm/card%d";
const char g_dev_card_path_format[] =
   "/dev/dri/card%d";

int drm_open_vgem()
{
	char *name;
	int i, fd;

	for (i = 0; i < 16; i++) {
		struct stat _stat;
		int ret;
		ret = asprintf(&name, g_sys_card_path_format, i);
		assert(ret != -1);

		if (stat(name, &_stat) == -1) {
			free(name);
			continue;
		}

		free(name);
		ret = asprintf(&name, g_dev_card_path_format, i);
		assert(ret != -1);

		fd = open(name, O_RDWR);
		free(name);
		if (fd == -1) {
			continue;
		}
		return fd;
	}
	return -1;
}

void * mmap_dumb_bo(int fd, int handle, size_t size)
{
	struct drm_mode_map_dumb mmap_arg;
	void *ptr;
	int ret;

	memset(&mmap_arg, 0, sizeof(mmap_arg));

	mmap_arg.handle = handle;

	ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mmap_arg);
	assert(ret == 0);
	assert(mmap_arg.offset != 0);

	ptr = mmap(NULL, size, (PROT_READ|PROT_WRITE), MAP_SHARED, fd,
		   mmap_arg.offset);

	assert(ptr != MAP_FAILED);

	return ptr;
}

bool setup_drm(struct context *ctx)
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

void show_sequence(const int *sequence)
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

void draw(struct context *ctx)
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
			size_t bo_stride = gbm_bo_get_stride(ctx->gbm_buffer[fb_idx]);
			size_t bo_size = gbm_bo_get_stride(ctx->gbm_buffer[fb_idx]) * gbm_bo_get_height(ctx->gbm_buffer[fb_idx]);
			uint32_t *bo_ptr;
			volatile uint32_t *ptr;

			for (sequence_subindex = 0; sequence_subindex < 4; sequence_subindex++) {
				switch (sequences[sequence_index][sequence_subindex]) {
				case STEP_MMAP:
					bo_ptr = mmap_dumb_bo(ctx->vgem_card_fd, ctx->vgem_bo_handle[fb_idx], bo_size);
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

			munmap(bo_ptr, bo_size);

			usleep(1e6 / 120); /* 120 Hz */

			fb_idx = fb_idx ^ 1;
		}
	}
}

int main(int argc, char **argv)
{
	int ret = 0;
	struct context ctx;
	uint32_t bo_handle;
	uint32_t bo_stride;
	int drm_prime_fd;
	size_t i;
	char *drm_card_path = "/dev/dri/card0";

	if (argc >= 2)
		drm_card_path = argv[1];

	do_fixes();

	ctx.drm_card_fd = open(drm_card_path, O_RDWR);
	if (ctx.drm_card_fd < 0) {
		fprintf(stderr, "failed to open %s\n", drm_card_path);
		ret = 1;
		goto fail;
	}

	ctx.vgem_card_fd = drm_open_vgem();
	if (ctx.vgem_card_fd < 0) {
		fprintf(stderr, "failed to open vgem card\n");
		ret = 1;
		goto close_drm_card;
	}

	ctx.drm_gbm = gbm_create_device(ctx.drm_card_fd);
	if (!ctx.drm_gbm) {
		fprintf(stderr, "failed to create gbm device on %s\n", drm_card_path);
		ret = 1;
		goto close_vgem_card;
	}

	if (!setup_drm(&ctx)) {
		fprintf(stderr, "failed to setup drm resources\n");
		ret = 1;
		goto destroy_drm_gbm;
	}

	fprintf(stderr, "display size: %dx%d\n",
		ctx.mode->hdisplay, ctx.mode->vdisplay);


	for (i = 0; i < BUFFERS; ++i) {
		ctx.gbm_buffer[i] = gbm_bo_create(ctx.drm_gbm,
			ctx.mode->hdisplay, ctx.mode->vdisplay, GBM_BO_FORMAT_XRGB8888,
			GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);

		if (!ctx.gbm_buffer[i]) {
			fprintf(stderr, "failed to create buffer object\n");
			ret = 1;
			goto free_buffers;
		}

		bo_handle = gbm_bo_get_handle(ctx.gbm_buffer[i]).u32;
		bo_stride = gbm_bo_get_stride(ctx.gbm_buffer[i]);

		drm_prime_fd = gbm_bo_get_fd(ctx.gbm_buffer[i]);

		if (drm_prime_fd < 0) {
			fprintf(stderr, "failed to turn handle into fd\n");
			ret = 1;
			goto free_buffers;
		}

		ret = drmPrimeFDToHandle(ctx.vgem_card_fd, drm_prime_fd,
					 &ctx.vgem_bo_handle[i]);
		if (ret) {
			fprintf(stderr, "failed to import handle\n");
			ret = 1;
			goto free_buffers;
		}

		ret = drmModeAddFB(ctx.drm_card_fd, ctx.mode->hdisplay, ctx.mode->vdisplay,
			24, 32, bo_stride, bo_handle, &ctx.drm_fb_id[i]);

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
		if (ctx.gbm_buffer[i])
			gbm_bo_destroy(ctx.gbm_buffer[i]);
	}

	drmModeFreeConnector(ctx.connector);
	drmModeFreeEncoder(ctx.encoder);
	drmModeFreeResources(ctx.resources);
destroy_drm_gbm:
	gbm_device_destroy(ctx.drm_gbm);
close_vgem_card:
	close(ctx.vgem_card_fd);
close_drm_card:
	close(ctx.drm_card_fd);
fail:
	return ret;
}
