/* basic set of prime tests between intel and nouveau */

/* test list -
   1. share buffer from intel -> nouveau.
   2. share buffer from nouveau -> intel
   3. share intel->nouveau, map on both, write intel, read nouveau
   4. share intel->nouveau, blit intel fill, readback on nouveau
   test 1 + map buffer, read/write, map other size.
   do some hw actions on the buffer
   some illegal operations -
       close prime fd try and map

   TODO add some nouveau rendering tests
*/


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "ioctl_wrappers.h"
#include "intel_bufmgr.h"
#include "nouveau.h"
#include "intel_io.h"
#include "intel_batchbuffer.h"
#include "intel_chipset.h"
#include "drmtest.h"

static int intel_fd = -1, nouveau_fd = -1;
static drm_intel_bufmgr *bufmgr;
static struct nouveau_device *ndev;
static struct nouveau_client *nclient;
static uint32_t devid;
static struct intel_batchbuffer *batch;
static struct nouveau_object *nchannel, *pcopy;
static struct nouveau_bufctx *nbufctx;
static struct nouveau_pushbuf *npush;

static struct nouveau_bo *query_bo;
static uint32_t query_counter;
static volatile uint32_t *query;
static uint32_t memtype_intel, tile_intel_y, tile_intel_x;

#define SUBC_COPY(x) 6, (x)
#define NV01_SUBCHAN_OBJECT 0

#define NV01_SUBC(subc, mthd) SUBC_##subc((NV01_SUBCHAN_##mthd))

typedef struct {
	uint32_t w, h;
	uint32_t pitch, lines;
} rect;

static void nv_bo_alloc(struct nouveau_bo **bo, rect *r,
			uint32_t w, uint32_t h, uint32_t tile_mode,
			int handle, uint32_t dom)
{
	uint32_t size;
	uint32_t dx = 1, dy = 1, memtype = 0;

	*bo = NULL;
	if (tile_mode) {
		uint32_t tile_y;
		uint32_t tile_x;

		/* Y major tiling */
		if ((tile_mode & 0xf) == 0xe)
			/* but the internal layout is different */
			tile_x = 7;
		else
			tile_x = 6 + (tile_mode & 0xf);
		if (ndev->chipset < 0xc0) {
			memtype = 0x70;
			tile_y = 2;
		} else {
			memtype = 0xfe;
			tile_y = 3;
		}
		if ((tile_mode & 0xf) == 0xe)
			memtype = memtype_intel;
		tile_y += ((tile_mode & 0xf0)>>4);

		dx = 1 << tile_x;
		dy = 1 << tile_y;
		igt_debug("Tiling requirements: x y %u %u\n", dx, dy);
	}

	r->w = w;
	r->h = h;

	r->pitch = w = (w + dx-1) & ~(dx-1);
	r->lines = h = (h + dy-1) & ~(dy-1);
	size = w*h;

	if (handle < 0) {
		union nouveau_bo_config cfg;
		cfg.nv50.memtype = memtype;
		cfg.nv50.tile_mode = tile_mode;
		if (dom == NOUVEAU_BO_GART)
			dom |= NOUVEAU_BO_MAP;
		igt_assert(nouveau_bo_new(ndev, dom, 4096, size, &cfg, bo) == 0);
		igt_assert(nouveau_bo_map(*bo, NOUVEAU_BO_RDWR, nclient) == 0);

		igt_debug("new flags %08x memtype %08x tile %08x\n",
			  (*bo)->flags, (*bo)->config.nv50.memtype,
			  (*bo)->config.nv50.tile_mode);
		if (tile_mode == tile_intel_y || tile_mode == tile_intel_x) {
			igt_debug("tile mode was: %02x, now: %02x\n",
				  (*bo)->config.nv50.tile_mode, tile_mode);
			/* Doesn't like intel tiling much.. */
			(*bo)->config.nv50.tile_mode = tile_mode;
		}
	} else {
		igt_assert(nouveau_bo_prime_handle_ref(ndev, handle, bo) == 0);
		close(handle);
		igt_assert_f((*bo)->size >= size,
			     "expected bo size to be at least %u,"
			     "but received %"PRIu64"\n", size, (*bo)->size);
		igt_debug("prime flags %08x memtype %08x tile %08x\n",
			  (*bo)->flags, (*bo)->config.nv50.memtype,
			  (*bo)->config.nv50.tile_mode);
		(*bo)->config.nv50.memtype = memtype;
		(*bo)->config.nv50.tile_mode = tile_mode;
	}
	igt_debug("size: %"PRIu64"\n", (*bo)->size);
}

static inline void
PUSH_DATA(struct nouveau_pushbuf *push, uint32_t data)
{
	*push->cur++ = data;
}

static inline void
BEGIN_NV04(struct nouveau_pushbuf *push, int subc, int mthd, int size)
{
	PUSH_DATA (push, 0x00000000 | (size << 18) | (subc << 13) | mthd);
}

static inline void
BEGIN_NI04(struct nouveau_pushbuf *push, int subc, int mthd, int size)
{
	PUSH_DATA (push, 0x40000000 | (size << 18) | (subc << 13) | mthd);
}

static inline void
BEGIN_NVC0(struct nouveau_pushbuf *push, int subc, int mthd, int size)
{
	PUSH_DATA (push, 0x20000000 | (size << 16) | (subc << 13) | (mthd / 4));
}

static inline void
BEGIN_NVXX(struct nouveau_pushbuf *push, int subc, int mthd, int size)
{
	if (ndev->chipset < 0xc0)
		BEGIN_NV04(push, subc, mthd, size);
	else
		BEGIN_NVC0(push, subc, mthd, size);
}

static void
noop_intel(drm_intel_bo *bo)
{
	BEGIN_BATCH(3);
	OUT_BATCH(MI_NOOP);
	OUT_BATCH(MI_BATCH_BUFFER_END);
	OUT_RELOC(bo, I915_GEM_DOMAIN_RENDER,
			I915_GEM_DOMAIN_RENDER, 0);
	ADVANCE_BATCH();

	intel_batchbuffer_flush(batch);
}

static void find_and_open_devices(void)
{
	int i;
	char path[80], *unused;
	struct stat buf;
	FILE *fl;
	char vendor_id[8] = {};
	int venid;
	for (i = 0; i < 9; i++) {
		sprintf(path, "/sys/class/drm/card%d/device/vendor", i);
		if (stat(path, &buf))
			break;

		fl = fopen(path, "r");
		if (!fl)
			break;

		unused = fgets(vendor_id, sizeof(vendor_id)-1, fl);
		(void)unused;
		fclose(fl);

		venid = strtoul(vendor_id, NULL, 16);
		sprintf(path, "/dev/dri/card%d", i);
		if (venid == 0x8086) {
			intel_fd = open(path, O_RDWR);
			igt_assert(intel_fd);
		} else if (venid == 0x10de) {
			nouveau_fd = open(path, O_RDWR);
			igt_assert(nouveau_fd);
		}
	}
}

static void init_nouveau(void)
{
	struct nv04_fifo nv04_data = { .vram = 0xbeef0201,
				       .gart = 0xbeef0202 };
	struct nvc0_fifo nvc0_data = { };
	struct nouveau_fifo *fifo;
	int size;
	uint32_t class;
	void *data;

	igt_assert(nouveau_device_wrap(nouveau_fd, 0, &ndev) == 0);

	igt_assert(nouveau_client_new(ndev, &nclient) == 0);

	igt_skip_on_f(ndev->chipset < 0xa3 || ndev->chipset == 0xaa || ndev->chipset == 0xac,
		      "Your card doesn't support PCOPY\n");

	// TODO: Get a kepler and add support for it
	igt_skip_on_f(ndev->chipset >= 0xe0,
		      "Unsure how kepler works!\n");
	igt_assert(nouveau_bo_new(ndev,  NOUVEAU_BO_GART | NOUVEAU_BO_MAP,
				  4096, 4096, NULL, &query_bo) == 0);
	igt_assert(nouveau_bo_map(query_bo, NOUVEAU_BO_RDWR, nclient) == 0);
	query = query_bo->map;
	*query = query_counter;

	if (ndev->chipset < 0xc0) {
		class = 0x85b5;
		data = &nv04_data;
		size = sizeof(nv04_data);
	} else {
		class = ndev->chipset < 0xe0 ? 0x490b5 : 0xa0b5;
		data = &nvc0_data;
		size = sizeof(nvc0_data);
	}

	igt_assert(nouveau_object_new(&ndev->object, 0, NOUVEAU_FIFO_CHANNEL_CLASS,
				      data, size, &nchannel) == 0);

	fifo = nchannel->data;

	igt_assert(nouveau_pushbuf_new(nclient, nchannel, 4, 32 * 1024,
				       true, &npush) == 0);

	igt_assert(nouveau_bufctx_new(nclient, 1, &nbufctx) == 0);

	npush->user_priv = nbufctx;

	/* Hope this is enough init for PCOPY */
	igt_assert(nouveau_object_new(nchannel, class, class & 0xffff, NULL, 0, &pcopy) == 0);
	igt_assert(nouveau_pushbuf_space(npush, 512, 0, 0) == 0);

	if (ndev->chipset < 0xc0) {
		struct nv04_fifo *nv04_fifo = (struct nv04_fifo*)fifo;
		tile_intel_y = 0x3e;
		tile_intel_x = 0x13;

		BEGIN_NV04(npush, NV01_SUBC(COPY, OBJECT), 1);
		PUSH_DATA(npush, pcopy->handle);
		BEGIN_NV04(npush, SUBC_COPY(0x0180), 3);
		PUSH_DATA(npush, nv04_fifo->vram);
		PUSH_DATA(npush, nv04_fifo->vram);
		PUSH_DATA(npush, nv04_fifo->vram);
	} else {
		tile_intel_y = 0x2e;
		tile_intel_x = 0x03;
		BEGIN_NVC0(npush, NV01_SUBC(COPY, OBJECT), 1);
		PUSH_DATA(npush, pcopy->handle);
	}
	nouveau_pushbuf_kick(npush, npush->channel);
}

static void fill16(void *ptr, uint32_t val)
{
	uint32_t *p = ptr;
	val = (val) | (val << 8) | (val << 16) | (val << 24);
	p[0] = p[1] = p[2] = p[3] = val;
}

#define TILE_SIZE 4096

static void swtile_y(uint8_t *out, const uint8_t *in, int w, int h)
{
	uint32_t x, y, dx, dy;
	uint8_t *endptr = out + w * h;
	igt_assert(!(w % 128));
	igt_assert(!(h % 32));

	for (y = 0; y < h; y += 32) {
		for (x = 0; x < w; x += 128, out += TILE_SIZE) {
			for (dx = 0; dx < 8; ++dx) {
				for (dy = 0; dy < 32; ++dy) {
					uint32_t out_ofs = (dx * 32 + dy) * 16;
					uint32_t in_ofs = (y + dy) * w + (x + 16 * dx);
					igt_assert(out_ofs < TILE_SIZE);
					igt_assert(in_ofs < w*h);

					// To do the Y tiling quirk:
					// out_ofs = out_ofs ^ (((out_ofs >> 9) & 1) << 6);
					memcpy(&out[out_ofs], &in[in_ofs], 16);
				}
			}
		}
	}
	igt_assert(out == endptr);
}

static void swtile_x(uint8_t *out, const uint8_t *in, int w, int h)
{
	uint32_t x, y, dy;
	uint8_t *endptr = out + w * h;
	igt_assert(!(w % 512));
	igt_assert(!(h % 8));

	for (y = 0; y < h; y += 8) {
		for (x = 0; x < w; x += 512, out += TILE_SIZE) {
			for (dy = 0; dy < 8; ++dy) {
				uint32_t out_ofs = 512 * dy;
				uint32_t in_ofs = (y + dy) * w + x;
				igt_assert(out_ofs < TILE_SIZE);
				igt_assert(in_ofs < w*h);
				memcpy(&out[out_ofs], &in[in_ofs], 512);
			}
		}
	}
	igt_assert(out == endptr);
}

static void perform_copy(struct nouveau_bo *nvbo, const rect *dst,
			 uint32_t dst_x, uint32_t dst_y,
			 struct nouveau_bo *nvbi, const rect *src,
			 uint32_t src_x, uint32_t src_y,
			 uint32_t w, uint32_t h)
{
	struct nouveau_pushbuf_refn refs[] = {
		{ nvbi, (nvbi->flags & NOUVEAU_BO_APER) | NOUVEAU_BO_RD },
		{ nvbo, (nvbo->flags & NOUVEAU_BO_APER) | NOUVEAU_BO_WR },
		{ query_bo, NOUVEAU_BO_GART | NOUVEAU_BO_RDWR }
	};
	uint32_t cpp = 1, exec = 0x00003000; /* QUERY|QUERY_SHORT|FORMAT */
	uint32_t src_off = 0, dst_off = 0;
	struct nouveau_pushbuf *push = npush;
	int ret;

	if (nvbi->config.nv50.tile_mode == tile_intel_y)
		igt_debug("src is y-tiled\n");
	if (nvbo->config.nv50.tile_mode == tile_intel_y)
		igt_debug("dst is y-tiled\n");

	igt_assert(nouveau_pushbuf_space(push, 64, 0, 0) == 0);
	igt_assert(nouveau_pushbuf_refn(push, refs, 3) == 0);

	if (!nvbi->config.nv50.tile_mode) {
		src_off = src_y * src->pitch + src_x;
		exec |= 0x00000010;
	}

	if (!nvbo->config.nv50.tile_mode) {
		dst_off = dst_y * dst->pitch + dst_x;
		exec |= 0x00000100;
	}

	BEGIN_NVXX(push, SUBC_COPY(0x0200), 7);
	PUSH_DATA (push, nvbi->config.nv50.tile_mode);
	PUSH_DATA (push, src->pitch / cpp);
	PUSH_DATA (push, src->h);
	PUSH_DATA (push, 1);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, src_x / cpp);
	PUSH_DATA (push, src_y);

	BEGIN_NVXX(push, SUBC_COPY(0x0220), 7);
	PUSH_DATA (push, nvbo->config.nv50.tile_mode);
	PUSH_DATA (push, dst->pitch / cpp);
	PUSH_DATA (push, dst->h);
	PUSH_DATA (push, 1);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, dst_x / cpp);
	PUSH_DATA (push, dst_y);

	BEGIN_NVXX(push, SUBC_COPY(0x030c), 9);
	PUSH_DATA (push, (nvbi->offset + src_off) >> 32);
	PUSH_DATA (push, (nvbi->offset + src_off));
	PUSH_DATA (push, (nvbo->offset + dst_off) >> 32);
	PUSH_DATA (push, (nvbo->offset + dst_off));
	PUSH_DATA (push, src->pitch);
	PUSH_DATA (push, dst->pitch);
	PUSH_DATA (push, w / cpp);
	PUSH_DATA (push, h);
	PUSH_DATA (push, 0x03333120);

	BEGIN_NVXX(push, SUBC_COPY(0x0338), 3);
	PUSH_DATA (push, (query_bo->offset) >> 32);
	PUSH_DATA (push, (query_bo->offset));
	PUSH_DATA (push, ++query_counter);

	BEGIN_NVXX(push, SUBC_COPY(0x0300), 1);
	PUSH_DATA (push, exec);

	ret = nouveau_pushbuf_kick(push, push->channel);
	while (!ret && *query < query_counter) { usleep(1000); }

	igt_assert(ret == 0);
}

static void check1_macro(uint32_t *p, uint32_t w, uint32_t h)
{
	uint32_t i, val, j;

	for (i = 0; i < 256; ++i, p += 4) {
		val = (i) | (i << 8) | (i << 16) | (i << 24);
		igt_assert_f(p[0] == val && p[1] == val && p[2] == val && p[3] == val,
			     "Retile check failed in first tile!\n"
			     "%08x %08x %08x %08x instead of %08x\n",
			     p[0], p[1], p[2], p[3], val);
	}

	val = 0x3e3e3e3e;
	for (i = 0; i < 256 * (w-1); ++i, p += 4) {
		igt_assert_f(p[0] == val && p[1] == val && p[2] == val && p[3] == val,
			     "Retile check failed in second tile!\n"
			     "%08x %08x %08x %08x instead of %08x\n",
			     p[0], p[1], p[2], p[3], val);
	}

	for (j = 1; j < h; ++j) {
		val = 0x7e7e7e7e;
		for (i = 0; i < 256; ++i, p += 4) {
			igt_assert_f(p[0] == val && p[1] == val && p[2] == val && p[3] == val,
				     "Retile check failed in third tile!\n"
				     "%08x %08x %08x %08x instead of %08x\n",
				     p[0], p[1], p[2], p[3], val);
		}

		val = 0xcececece;
		for (i = 0; i < 256 * (w-1); ++i, p += 4) {
			igt_assert_f(p[0] == val && p[1] == val && p[2] == val && p[3] == val,
				     "Retile check failed in fourth tile!\n"
				     "%08x %08x %08x %08x instead of %08x\n",
				     p[0], p[1], p[2], p[3], val);
		}
	}
}

/* test 1, see if we can copy from linear to intel Y format safely */
static void test1_macro(void)
{
	int prime_fd = -1;
	struct nouveau_bo *nvbo = NULL, *nvbi = NULL;
	rect dst, src;
	uint8_t *ptr;
	uint32_t w = 2 * 128, h = 2 * 32, x, y;

	nv_bo_alloc(&nvbi, &src, w, h, 0, -1, NOUVEAU_BO_GART);
	nv_bo_alloc(&nvbo, &dst, w, h, tile_intel_y, -1, NOUVEAU_BO_GART);

	nouveau_bo_set_prime(nvbo, &prime_fd);

	/* Set up something for our tile that should map into the first
	 * y-major tile, assuming my understanding of documentation is
	 * correct
	 */

	/* First tile should be read out in groups of 16 bytes that
	 * are all set to a linear increasing value..
	 */
	ptr = nvbi->map;
	for (x = 0; x < 128; x += 16)
		for (y = 0; y < 32; ++y)
			fill16(&ptr[y * w + x], x * 2 + y);

	/* second tile */
	for (x = 128; x < w; x += 16)
		for (y = 0; y < 32; ++y)
			fill16(&ptr[y * w + x], 0x3e);

	/* third tile */
	for (x = 0; x < 128; x += 16)
		for (y = 32; y < h; ++y)
			fill16(&ptr[y * w + x], 0x7e);

	/* last tile */
	for (x = 128; x < w; x += 16)
		for (y = 32; y < h; ++y)
			fill16(&ptr[y * w + x], 0xce);
	memset(nvbo->map, 0xfc, w * h);

	if (pcopy)
		perform_copy(nvbo, &dst, 0, 0, nvbi, &src, 0, 0, w, h);
	else
		swtile_y(nvbo->map, nvbi->map, w, h);
	check1_macro(nvbo->map, w/128, h/32);

	nouveau_bo_ref(NULL, &nvbo);
	nouveau_bo_ref(NULL, &nvbi);
	close(prime_fd);
}

static void dump_line(uint8_t *map)
{
	uint32_t dx, dy;
	igt_debug("Dumping sub-tile:\n");
	for (dy = 0; dy < 32; ++dy) {
		for (dx = 0; dx < 15; ++dx, ++map) {
			igt_debug("%02x ", *map);
		}
		igt_debug("%02x\n", *(map++));
	}
}

static void check1_micro(void *map, uint32_t pitch, uint32_t lines,
			 uint32_t dst_x, uint32_t dst_y, uint32_t w, uint32_t h)
{
	uint32_t x, y;

	/* check only the relevant subrectangle [0..w) [0...h) */
	uint8_t *m = map;
	for (y = 0; y < h; ++y, m += pitch) {
		for (x = 0; x < w; ++x) {
			uint8_t expected = ((y & 3) << 6) | (x & 0x3f);

			if (expected != m[x])
				dump_line(m);

			igt_assert_f(expected == m[x],
				     "failed check at x=%u y=%u, expected %02x got %02x\n",
				     x, y, expected, m[x]);
		}
	}
}

/* test 1, but check micro format, should be unaffected by bit9 swizzling */
static void test1_micro(void)
{
	struct nouveau_bo *bo_intel = NULL, *bo_nvidia = NULL, *bo_linear = NULL;
	rect intel, nvidia, linear;
	uint32_t tiling = I915_TILING_Y;

	uint32_t src_x = 0, src_y = 0;
	uint32_t dst_x = 0, dst_y = 0;
	uint32_t x, y, w = 256, h = 64;

	drm_intel_bo *test_intel_bo;
	int prime_fd;

	test_intel_bo = drm_intel_bo_alloc(bufmgr, "test bo", w * h, 4096);
	igt_assert(test_intel_bo);
	drm_intel_bo_set_tiling(test_intel_bo, &tiling, w);
	igt_assert(tiling == I915_TILING_Y);
	igt_assert(drm_intel_gem_bo_map_gtt(test_intel_bo) == 0);

	drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd);
	igt_assert(prime_fd >= 0);
	noop_intel(test_intel_bo);

	nv_bo_alloc(&bo_intel, &intel, w, h, tile_intel_y, prime_fd, 0);
	nv_bo_alloc(&bo_nvidia, &nvidia, w, h, 0x10, -1, NOUVEAU_BO_VRAM);
	nv_bo_alloc(&bo_linear, &linear, w, h, 0, -1, NOUVEAU_BO_GART);

	for (y = 0; y < linear.h; ++y) {
		uint8_t *map = bo_linear->map;
		map += y * linear.pitch;
		for (x = 0; x < linear.pitch; ++x) {
			uint8_t pos = x & 0x3f;
			/* low 4 bits: micro tile pos */
			/* 2 bits: x pos in tile (wraps) */
			/* 2 bits: y pos in tile (wraps) */
			pos |= (y & 3) << 6;
			map[x] = pos;
		}
	}

	perform_copy(bo_nvidia, &nvidia, 0, 0, bo_linear, &linear, 0, 0, nvidia.pitch, nvidia.h);

	/* Perform the actual sub rectangle copy */
	if (pcopy)
		perform_copy(bo_intel, &intel, dst_x, dst_y, bo_nvidia, &nvidia, src_x, src_y, w, h);
	else
		swtile_y(test_intel_bo->virtual, bo_linear->map, w, h);

	noop_intel(test_intel_bo);
	check1_micro(test_intel_bo->virtual, intel.pitch, intel.h, dst_x, dst_y, w, h);

	nouveau_bo_ref(NULL, &bo_linear);
	nouveau_bo_ref(NULL, &bo_nvidia);
	nouveau_bo_ref(NULL, &bo_intel);
	drm_intel_bo_unreference(test_intel_bo);
}

/* test 2, see if we can copy from linear to intel X format safely
 * Seems nvidia lacks a method to do it, so just keep this test
 * as a reference for potential future tests. Software tiling is
 * used for now
 */
static void test2(void)
{
	struct nouveau_bo *nvbo = NULL, *nvbi = NULL;
	rect dst, src;
	uint8_t *ptr;
	uint32_t w = 1024, h = 16, x, y;

	nv_bo_alloc(&nvbi, &src, w, h, 0, -1, NOUVEAU_BO_GART);
	nv_bo_alloc(&nvbo, &dst, w, h, tile_intel_x, -1, NOUVEAU_BO_GART);

	/* Set up something for our tile that should map into the first
	 * y-major tile, assuming my understanding of documentation is
	 * correct
	 */

	/* First tile should be read out in groups of 16 bytes that
	 * are all set to a linear increasing value..
	 */
	ptr = nvbi->map;
	for (y = 0; y < 8; ++y)
		for (x = 0; x < 512; x += 16)
			fill16(&ptr[y * w + x], (y * 512 + x)/16);

	for (y = 0; y < 8; ++y)
		for (x = 512; x < w; x += 16)
			fill16(&ptr[y * w + x], 0x3e);

	for (y = 8; y < h; ++y)
		for (x = 0; x < 512; x += 16)
			fill16(&ptr[y * w + x], 0x7e);

	for (y = 8; y < h; ++y)
		for (x = 512; x < w; x += 16)
			fill16(&ptr[y * w + x], 0xce);
	memset(nvbo->map, 0xfc, w * h);

	/* do this in software, there is no X major tiling in PCOPY (yet?) */
	if (0 && pcopy)
		perform_copy(nvbo, &dst, 0, 0, nvbi, &src, 0, 0, w, h);
	else
		swtile_x(nvbo->map, nvbi->map, w, h);
	check1_macro(nvbo->map, w/512, h/8);

	nouveau_bo_ref(NULL, &nvbo);
	nouveau_bo_ref(NULL, &nvbi);
}

static void check3(const uint32_t *p, uint32_t pitch, uint32_t lines,
		   uint32_t sub_x, uint32_t sub_y,
		   uint32_t sub_w, uint32_t sub_h)
{
	uint32_t x, y;

	sub_w += sub_x;
	sub_h += sub_y;

	igt_assert_f(p[pitch * lines / 4 - 1] != 0x03030303,
		     "copy failed: Not all lines have been copied back!\n");

	for (y = 0; y < lines; ++y) {
		for (x = 0; x < pitch; x += 4, ++p) {
			uint32_t expected;
			if ((x < sub_x || x >= sub_w) ||
			    (y < sub_y || y >= sub_h))
				expected = 0x80808080;
			else
				expected = 0x04040404;
			igt_assert_f(*p == expected,
				     "%u,%u should be %08x, but is %08x\n",
				     x, y, expected, *p);
		}
	}
}

/* copy from nvidia bo to intel bo and copy to a linear bo to check if tiling went succesful */
static void test3_base(int tile_src, int tile_dst)
{
	struct nouveau_bo *bo_intel = NULL, *bo_nvidia = NULL, *bo_linear = NULL;
	rect intel, nvidia, linear;
	uint32_t cpp = 4;

	uint32_t src_x = 1 * cpp, src_y = 1;
	uint32_t dst_x = 2 * cpp, dst_y = 26;
	uint32_t w = 298 * cpp, h = 298;

	drm_intel_bo *test_intel_bo;
	int prime_fd;

	test_intel_bo = drm_intel_bo_alloc(bufmgr, "test bo", 2048 * cpp * 768, 4096);
	igt_assert(test_intel_bo);

	drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd);
	igt_assert(prime_fd >= 0);

	nv_bo_alloc(&bo_intel, &intel, 2048 * cpp, 768, tile_dst, prime_fd, 0);
	nv_bo_alloc(&bo_nvidia, &nvidia, 300 * cpp, 300, tile_src, -1, NOUVEAU_BO_VRAM);
	nv_bo_alloc(&bo_linear, &linear, 2048 * cpp, 768, 0, -1, NOUVEAU_BO_GART);

	noop_intel(test_intel_bo);
	memset(bo_linear->map, 0x80, bo_linear->size);
	perform_copy(bo_intel, &intel, 0, 0, bo_linear, &linear, 0, 0, linear.pitch, linear.h);
	noop_intel(test_intel_bo);

	memset(bo_linear->map, 0x04, bo_linear->size);
	perform_copy(bo_nvidia, &nvidia, 0, 0, bo_linear, &linear, 0, 0, nvidia.pitch, nvidia.h);

	/* Perform the actual sub rectangle copy */
	noop_intel(test_intel_bo);
	perform_copy(bo_intel, &intel, dst_x, dst_y, bo_nvidia, &nvidia, src_x, src_y, w, h);
	noop_intel(test_intel_bo);

	memset(bo_linear->map, 0x3, bo_linear->size);
	noop_intel(test_intel_bo);
	perform_copy(bo_linear, &linear, 0, 0, bo_intel, &intel, 0, 0, intel.pitch, intel.h);
	noop_intel(test_intel_bo);

	check3(bo_linear->map, linear.pitch, linear.h, dst_x, dst_y, w, h);

	nouveau_bo_ref(NULL, &bo_linear);
	nouveau_bo_ref(NULL, &bo_nvidia);
	nouveau_bo_ref(NULL, &bo_intel);
	drm_intel_bo_unreference(test_intel_bo);
}

static void test3_1(void)
{
	/* nvidia tiling to intel */
	test3_base(0x40, tile_intel_y);
}

static void test3_2(void)
{
	/* intel tiling to nvidia */
	test3_base(tile_intel_y, 0x40);
}

static void test3_3(void)
{
	/* intel tiling to linear */
	test3_base(tile_intel_y, 0);
}

static void test3_4(void)
{
	/* linear tiling to intel */
	test3_base(0, tile_intel_y);
}

static void test3_5(void)
{
	/* linear to linear */
	test3_base(0, 0);
}

/* Acquire when == SEQUENCE */
#define SEMA_ACQUIRE_EQUAL 1

/* Release, and write a 16 byte query structure to sema:
 * { (uint32)seq, (uint32)0, (uint64)timestamp } */
#define SEMA_WRITE_LONG 2

/* Acquire when >= SEQUENCE */
#define SEMA_ACQUIRE_GEQUAL 4

/* Test only new style semaphores, old ones are AWFUL */
static void test_semaphore(void)
{
	drm_intel_bo *test_intel_bo = NULL;
	struct nouveau_bo *sema_bo = NULL;
	int prime_fd;
	uint32_t *sema;
	struct nouveau_pushbuf *push = npush;

	igt_skip_on(ndev->chipset < 0x84);

	/* Should probably be kept in sysmem */
	test_intel_bo = drm_intel_bo_alloc(bufmgr, "semaphore bo", 4096, 4096);
	igt_assert(test_intel_bo);

	drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd);
	igt_assert(prime_fd >= 0);
	igt_assert(nouveau_bo_prime_handle_ref(ndev, prime_fd, &sema_bo) == 0);
	close(prime_fd);

	igt_assert(drm_intel_gem_bo_map_gtt(test_intel_bo) == 0);
	sema = test_intel_bo->virtual;
	sema++;
	*sema = 0;

	igt_assert(nouveau_pushbuf_space(push, 64, 0, 0) == 0);
	igt_assert(nouveau_pushbuf_refn(push, &(struct nouveau_pushbuf_refn)
					{ sema_bo, NOUVEAU_BO_GART|NOUVEAU_BO_RDWR }, 1) == 0);

	if (ndev->chipset < 0xc0) {
		struct nv04_fifo *nv04_fifo = nchannel->data;
		/* kernel binds it's own dma object here and overwrites old one,
		 * so just rebind vram every time we submit
		 */
		BEGIN_NV04(npush, SUBC_COPY(0x0060), 1);
		PUSH_DATA(npush, nv04_fifo->vram);
	}
	BEGIN_NVXX(push, SUBC_COPY(0x0010), 4);
	PUSH_DATA(push, sema_bo->offset >> 32);
	PUSH_DATA(push, sema_bo->offset + 4);
	PUSH_DATA(push, 2); // SEQUENCE
	PUSH_DATA(push, SEMA_WRITE_LONG); // TRIGGER

	BEGIN_NVXX(push, SUBC_COPY(0x0018), 2);
	PUSH_DATA(push, 3);
	PUSH_DATA(push, SEMA_ACQUIRE_EQUAL);
	BEGIN_NVXX(push, SUBC_COPY(0x0018), 2);
	PUSH_DATA(push, 4);
	PUSH_DATA(push, SEMA_WRITE_LONG);

	BEGIN_NVXX(push, SUBC_COPY(0x0018), 2);
	PUSH_DATA(push, 5);
	PUSH_DATA(push, SEMA_ACQUIRE_GEQUAL);
	BEGIN_NVXX(push, SUBC_COPY(0x0018), 2);
	PUSH_DATA(push, 6);
	PUSH_DATA(push, SEMA_WRITE_LONG);

	BEGIN_NVXX(push, SUBC_COPY(0x0018), 2);
	PUSH_DATA(push, 7);
	PUSH_DATA(push, SEMA_ACQUIRE_GEQUAL);
	BEGIN_NVXX(push, SUBC_COPY(0x0018), 2);
	PUSH_DATA(push, 9);
	PUSH_DATA(push, SEMA_WRITE_LONG);
	nouveau_pushbuf_kick(push, push->channel);

	usleep(1000);
	igt_assert(*sema == 2);

	*sema = 3;
	usleep(1000);
	igt_assert(*sema == 4);

	*sema = 5;
	usleep(1000);
	igt_assert(*sema == 6);

	*sema = 8;
	usleep(1000);
	igt_assert(*sema == 9);

	nouveau_bo_ref(NULL, &sema_bo);
	drm_intel_bo_unreference(test_intel_bo);
}

igt_main
{
	igt_fixture {
		find_and_open_devices();

		igt_require(nouveau_fd != -1);
		igt_require(intel_fd != -1);

		/* set up intel bufmgr */
		bufmgr = drm_intel_bufmgr_gem_init(intel_fd, 4096);
		igt_assert(bufmgr);
		/* Do not enable reuse, we share (almost) all buffers. */
		//drm_intel_bufmgr_gem_enable_reuse(bufmgr);

		/* set up nouveau bufmgr */
		init_nouveau();

		/* set up an intel batch buffer */
		devid = intel_get_drm_devid(intel_fd);
		batch = intel_batchbuffer_alloc(bufmgr, devid);
		igt_assert(batch);
	}

#define xtest(x, args...) \
	igt_subtest( #x ) \
		(x)(args);

	xtest(test1_macro);
	xtest(test1_micro);
	//xtest(test1_swizzle);
	xtest(test2);
	xtest(test3_1);
	xtest(test3_2);
	xtest(test3_3);
	xtest(test3_4);
	xtest(test3_5);
	xtest(test_semaphore);

	igt_fixture {
		nouveau_bo_ref(NULL, &query_bo);
		nouveau_object_del(&pcopy);
		nouveau_bufctx_del(&nbufctx);
		nouveau_pushbuf_del(&npush);
		nouveau_object_del(&nchannel);

		intel_batchbuffer_free(batch);

		nouveau_client_del(&nclient);
		nouveau_device_del(&ndev);
		drm_intel_bufmgr_destroy(bufmgr);

		close(intel_fd);
		close(nouveau_fd);
	}
}
