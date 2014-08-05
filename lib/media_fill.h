#ifndef RENDE_MEDIA_FILL_H
#define RENDE_MEDIA_FILL_H

#include <stdint.h>
#include "intel_batchbuffer.h"

void
gen8_media_fillfunc(struct intel_batchbuffer *batch,
		struct igt_buf *dst,
		unsigned x, unsigned y,
		unsigned width, unsigned height,
		uint8_t color);

void
gen7_media_fillfunc(struct intel_batchbuffer *batch,
                struct igt_buf *dst,
                unsigned x, unsigned y,
                unsigned width, unsigned height,
                uint8_t color);

void
gen8lp_media_fillfunc(struct intel_batchbuffer *batch,
		struct igt_buf *dst,
		unsigned x, unsigned y,
		unsigned width, unsigned height,
		uint8_t color);

#endif /* RENDE_MEDIA_FILL_H */
