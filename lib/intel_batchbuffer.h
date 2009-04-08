#ifndef INTEL_BATCHBUFFER_H
#define INTEL_BATCHBUFFER_H

#include <assert.h>
#include "intel_bufmgr.h"
#include "intel_reg.h"

#define BATCH_SZ 4096
#define BATCH_RESERVED 16

struct intel_batchbuffer
{
	drm_intel_bufmgr *bufmgr;

	drm_intel_bo *bo;

	uint8_t *buffer;

	uint8_t *map;
	uint8_t *ptr;

	/* debug stuff */
	struct {
		uint8_t *start_ptr;
		unsigned int total;
	} emit;

	unsigned int size;
};

struct intel_batchbuffer *intel_batchbuffer_alloc(drm_intel_bufmgr *bufmgr);

void intel_batchbuffer_free(struct intel_batchbuffer *batch);


void intel_batchbuffer_flush(struct intel_batchbuffer *batch);

void intel_batchbuffer_reset(struct intel_batchbuffer *batch);

void intel_batchbuffer_data(struct intel_batchbuffer *batch,
                            const void *data, unsigned int bytes);

void intel_batchbuffer_emit_reloc(struct intel_batchbuffer *batch,
				  drm_intel_bo *buffer,
				  uint32_t delta,
				  uint32_t read_domains,
				  uint32_t write_domain);

/* Inline functions - might actually be better off with these
 * non-inlined.  Certainly better off switching all command packets to
 * be passed as structs rather than dwords, but that's a little bit of
 * work...
 */
static inline int
intel_batchbuffer_space(struct intel_batchbuffer *batch)
{
	return (batch->size - BATCH_RESERVED) - (batch->ptr - batch->map);
}


static inline void
intel_batchbuffer_emit_dword(struct intel_batchbuffer *batch, uint32_t dword)
{
	assert(batch->map);
	assert(intel_batchbuffer_space(batch) >= 4);
	*(uint32_t *) (batch->ptr) = dword;
	batch->ptr += 4;
}

static inline void
intel_batchbuffer_require_space(struct intel_batchbuffer *batch,
                                unsigned int sz)
{
	assert(sz < batch->size - 8);
	if (intel_batchbuffer_space(batch) < sz)
		intel_batchbuffer_flush(batch);
}

/* Here are the crusty old macros, to be removed:
 */
#define BATCH_LOCALS

#define BEGIN_BATCH(n) do {						\
	intel_batchbuffer_require_space(batch, (n)*4);			\
	assert(batch->emit.start_ptr == NULL);				\
	batch->emit.total = (n) * 4;					\
	batch->emit.start_ptr = batch->ptr;				\
} while (0)

#define OUT_BATCH(d) intel_batchbuffer_emit_dword(batch, d)

#define OUT_RELOC(buf, read_domains, write_domain, delta) do {		\
	assert((delta) >= 0);						\
	intel_batchbuffer_emit_reloc(batch, buf, delta,			\
				     read_domains, write_domain);	\
} while (0)

#define ADVANCE_BATCH() do {						\
	unsigned int _n = batch->ptr - batch->emit.start_ptr;		\
	assert(batch->emit.start_ptr != NULL);				\
	if (_n != batch->emit.total) {					\
		fprintf(stderr,						\
			"ADVANCE_BATCH: %d of %d dwords emitted\n",	\
			_n, batch->emit.total);				\
		abort();						\
	}								\
	batch->emit.start_ptr = NULL;					\
} while(0)


static inline void
intel_batchbuffer_emit_mi_flush(struct intel_batchbuffer *batch)
{
	intel_batchbuffer_require_space(batch, 4);
	intel_batchbuffer_emit_dword(batch, MI_FLUSH);
}

#endif
