#ifndef RENDE_MEDIA_FILL_H
#define RENDE_MEDIA_FILL_H

#include <stdlib.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <getopt.h>
#include "drm.h"
#include "i915_drm.h"
#include "drmtest.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_gpu_tools.h"

typedef void (*media_fillfunc_t)(struct intel_batchbuffer *batch,
				struct scratch_buf *dst,
				unsigned x, unsigned y,
				unsigned width, unsigned height,
				uint8_t color);

media_fillfunc_t get_media_fillfunc(int devid);

void
gen8_media_fillfunc(struct intel_batchbuffer *batch,
		struct scratch_buf *dst,
		unsigned x, unsigned y,
		unsigned width, unsigned height,
		uint8_t color);

void
gen7_media_fillfunc(struct intel_batchbuffer *batch,
                struct scratch_buf *dst,
                unsigned x, unsigned y,
                unsigned width, unsigned height,
                uint8_t color);

#endif /* RENDE_MEDIA_FILL_H */
