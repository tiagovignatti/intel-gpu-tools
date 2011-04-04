/*
 * Copyright © 2011 Daniel Vetter
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 * Partially based upon gem_tiled_fence_blits.c
 */

/** @file gem_stress.c
 *
 * This is a general gem coherency test. It's designed to eventually replicate
 * any possible sequence of access patterns. It works by copying a set of tiles
 * between two sets of backing buffer objects, randomly permutating the assinged
 * position on each copy operations.
 *
 * The copy operation are done in tiny portions (to reduce any race windows
 * for corruptions, hence increasing the chances for observing one) and are
 * constantly switched between all means to copy stuff (fenced blitter, unfenced
 * render, mmap, pwrite/read).
 *
 * After every complete move of a set tiling parameters of a buffer are randomly
 * changed to simulate the effects of libdrm caching.
 *
 * Buffers are 1mb big to nicely fit into fences on gen2/3. A few are further
 * split up to test relaxed fencing. Using this to push the average working set
 * size over the available gtt space forces objects to be mapped as unfenceable
 * (and as a side-effect tests gtt map/unmap coherency).
 *
 * In short: designed for maximum evilness.
 */

#include "gem_stress.h"

#define CMD_POLY_STIPPLE_OFFSET       0x7906

/** TODO:
 * - beat on relaxed fencing (i.e. mappable/fenceable tracking in the kernel)
 * - render copy (to check fence tracking and cache coherency management by the
 *   kernel)
 * - multi-threading: probably just a wrapper script to launch multiple
 *   instances + an option to accordingly reduce the working set
 * - gen6 inter-ring coherency (needs render copy, first)
 * - variable buffer size
 * - add an option to fork a second process that randomly sends signals to the
 *   first one (to check consistency of the kernel recovery paths)
 */

static uint64_t gem_aperture_size(int fd)
{
	struct drm_i915_gem_get_aperture aperture;

	aperture.aper_size = 256*1024*1024;
	(void)drmIoctl(fd, DRM_IOCTL_I915_GEM_GET_APERTURE, &aperture);
	return aperture.aper_size;
}

drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;
int drm_fd;
int devid;
int num_fences;

drm_intel_bo *busy_bo;

struct option_struct options;

static struct scratch_buf buffers[2][MAX_BUFS];
/* tile i is at logical position tile_permutation[i] */
static unsigned *tile_permutation;
static unsigned num_buffers = 0;
static unsigned current_set = 0;
static unsigned target_set = 0;
static unsigned num_total_tiles = 0;

#define TILES_PER_BUF		(num_total_tiles / num_buffers)

int fence_storm = 0;
static int gpu_busy_load = 10;

static void tile2xy(struct scratch_buf *buf, unsigned tile, unsigned *x, unsigned *y)
{
	assert(tile < buf->num_tiles);
	*x = (tile*TILE_SIZE) % (buf->stride/sizeof(uint32_t));
	*y = ((tile*TILE_SIZE) / (buf->stride/sizeof(uint32_t))) * TILE_SIZE;
}

static void emit_blt(drm_intel_bo *src_bo, uint32_t src_tiling, unsigned src_pitch,
		     unsigned src_x, unsigned src_y, unsigned w, unsigned h,
		     drm_intel_bo *dst_bo, uint32_t dst_tiling, unsigned dst_pitch,
		     unsigned dst_x, unsigned dst_y)
{
	uint32_t cmd_bits = 0;

	if (IS_965(devid) && src_tiling) {
		src_pitch /= 4;
		cmd_bits |= XY_SRC_COPY_BLT_SRC_TILED;
	}

	if (IS_965(devid) && dst_tiling) {
		dst_pitch /= 4;
		cmd_bits |= XY_SRC_COPY_BLT_DST_TILED;
	}

	/* copy lower half to upper half */
	BEGIN_BATCH(8);
	OUT_BATCH(XY_SRC_COPY_BLT_CMD |
		  XY_SRC_COPY_BLT_WRITE_ALPHA |
		  XY_SRC_COPY_BLT_WRITE_RGB |
		  cmd_bits);
	OUT_BATCH((3 << 24) | /* 32 bits */
		  (0xcc << 16) | /* copy ROP */
		  dst_pitch);
	OUT_BATCH(dst_y << 16 | dst_x);
	OUT_BATCH((dst_y+h) << 16 | (dst_x+w));
	OUT_RELOC_FENCED(dst_bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
	OUT_BATCH(src_y << 16 | src_x);
	OUT_BATCH(src_pitch);
	OUT_RELOC_FENCED(src_bo, I915_GEM_DOMAIN_RENDER, 0, 0);
	ADVANCE_BATCH();
}

/* All this gem trashing wastes too much cpu time, so give the gpu something to
 * do to increase changes for races. */
void keep_gpu_busy(void)
{
	int tmp;

	tmp = 1 << gpu_busy_load;
	assert(tmp <= 1024);

	emit_blt(busy_bo, 0, 4096, 0, 0, tmp, 128,
		 busy_bo, 0, 4096, 0, 128);
}

static unsigned int copyfunc_seq = 0;
static void (*copyfunc)(struct scratch_buf *src, unsigned src_x, unsigned src_y,
			struct scratch_buf *dst, unsigned dst_x, unsigned dst_y,
			unsigned logical_tile_no);

/* stride, x, y in units of uint32_t! */
static void cpucpy2d(uint32_t *src, unsigned src_stride, unsigned src_x, unsigned src_y,
		     uint32_t *dst, unsigned dst_stride, unsigned dst_x, unsigned dst_y,
		     unsigned logical_tile_no)
{
	int i, j;
	int failed = 0;

	for (i = 0; i < TILE_SIZE; i++) {
		for (j = 0; j < TILE_SIZE; j++) {
			unsigned dst_ofs = dst_x + j + dst_stride * (dst_y + i);
			unsigned src_ofs = src_x + j + src_stride * (src_y + i);
			unsigned expect = logical_tile_no*TILE_SIZE*TILE_SIZE
			    + i*TILE_SIZE + j;
			uint32_t tmp = src[src_ofs];
			if (tmp != expect) {
			    printf("mismatch at tile %i pos %i, read %i, expected %i, diff %i\n",
				    logical_tile_no, i*TILE_SIZE + j, tmp, expect, (int) tmp - expect);
			    if (options.trace_tile >= 0)
				    exit(1);
			    failed = 1;
			}
			dst[dst_ofs] = tmp;
		}
	}
	if (failed)
		exit(1);
}

static void cpu_copyfunc(struct scratch_buf *src, unsigned src_x, unsigned src_y,
			 struct scratch_buf *dst, unsigned dst_x, unsigned dst_y,
			 unsigned logical_tile_no)
{
	cpucpy2d(src->data, src->stride/sizeof(uint32_t), src_x, src_y,
		 dst->data, dst->stride/sizeof(uint32_t), dst_x, dst_y,
		 logical_tile_no);
}

static void prw_copyfunc(struct scratch_buf *src, unsigned src_x, unsigned src_y,
			 struct scratch_buf *dst, unsigned dst_x, unsigned dst_y,
			 unsigned logical_tile_no)
{
	uint32_t tmp_tile[TILE_SIZE*TILE_SIZE];
	int i;

	if (src->tiling == I915_TILING_NONE) {
		for (i = 0; i < TILE_SIZE; i++) {
			unsigned ofs = src_x*sizeof(uint32_t) + src->stride*(src_y + i);
			drm_intel_bo_get_subdata(src->bo, ofs,
						 TILE_SIZE*sizeof(uint32_t),
						 tmp_tile + TILE_SIZE*i);
		}
	} else {
		cpucpy2d(src->data, src->stride/sizeof(uint32_t), src_x, src_y,
			 tmp_tile, TILE_SIZE, 0, 0, logical_tile_no);
	}

	if (dst->tiling == I915_TILING_NONE) {
		for (i = 0; i < TILE_SIZE; i++) {
			unsigned ofs = dst_x*sizeof(uint32_t) + dst->stride*(dst_y + i);
			drm_intel_bo_subdata(dst->bo, ofs,
					     TILE_SIZE*sizeof(uint32_t),
					     tmp_tile + TILE_SIZE*i);
		}
	} else {
		cpucpy2d(tmp_tile, TILE_SIZE, 0, 0,
			 dst->data, dst->stride/sizeof(uint32_t), dst_x, dst_y,
			 logical_tile_no);
	}
}

static void blitter_copyfunc(struct scratch_buf *src, unsigned src_x, unsigned src_y,
			     struct scratch_buf *dst, unsigned dst_x, unsigned dst_y,
			     unsigned logical_tile_no)
{
	static unsigned keep_gpu_busy_counter = 0;

	/* check both edges of the fence usage */
	if (keep_gpu_busy_counter & 1 && !fence_storm)
		keep_gpu_busy();

	emit_blt(src->bo, src->tiling, src->stride, src_x, src_y,
		 TILE_SIZE, TILE_SIZE,
		 dst->bo, dst->tiling, dst->stride, dst_x, dst_y);

	if (!(keep_gpu_busy_counter & 1) && !fence_storm)
		keep_gpu_busy();

	keep_gpu_busy_counter++;

	if (src->tiling)
		fence_storm--;
	if (dst->tiling)
		fence_storm--;

	if (fence_storm <= 1) {
		fence_storm = 0;
		intel_batchbuffer_flush(batch);
	}
}

static void render_copyfunc(struct scratch_buf *src, unsigned src_x, unsigned src_y,
			    struct scratch_buf *dst, unsigned dst_x, unsigned dst_y,
			    unsigned logical_tile_no)
{
	if (IS_GEN2(devid))
		gen2_render_copyfunc(src, src_x, src_y,
				     dst, dst_x, dst_y,
				     logical_tile_no);
	else if (IS_GEN3(devid))
		gen3_render_copyfunc(src, src_x, src_y,
				     dst, dst_x, dst_y,
				     logical_tile_no);
	else
		blitter_copyfunc(src, src_x, src_y,
				 dst, dst_x, dst_y,
				 logical_tile_no);
}

static void next_copyfunc(int tile)
{
	if (fence_storm) {
		if (tile == options.trace_tile)
		printf(" using fence storm\n");
		return;
	}

	if (copyfunc_seq % 61 == 0) {
		if (tile == options.trace_tile)
			printf(" using fence storm\n");
		fence_storm = num_fences;
		copyfunc = blitter_copyfunc;
	} else if (copyfunc_seq % 17 == 0) {
		if (tile == options.trace_tile)
			printf(" using cpu\n");
		copyfunc = cpu_copyfunc;
	} else if (copyfunc_seq % 19 == 0) {
		if (tile == options.trace_tile)
			printf(" using prw\n");
		copyfunc = prw_copyfunc;
	} else if (copyfunc_seq % 3 == 0 && options.use_render) {
		if (tile == options.trace_tile)
			printf(" using render\n");
		copyfunc = render_copyfunc;
	} else {
		if (tile == options.trace_tile)
			printf(" using blitter\n");
		copyfunc = blitter_copyfunc;
	}

	copyfunc_seq++;
}

static void fan_out(void)
{
	uint32_t tmp_tile[TILE_SIZE*TILE_SIZE];
	uint32_t seq = 0;
	int i, k;
	unsigned tile, buf_idx, x, y;

	for (i = 0; i < num_total_tiles; i++) {
		tile = i;
		buf_idx = tile / TILES_PER_BUF;
		tile %= TILES_PER_BUF;

		tile2xy(&buffers[current_set][buf_idx], tile, &x, &y);

		for (k = 0; k < TILE_SIZE*TILE_SIZE; k++)
			tmp_tile[k] = seq++;

		cpucpy2d(tmp_tile, TILE_SIZE, 0, 0,
			 buffers[current_set][buf_idx].data,
			 buffers[current_set][buf_idx].stride / sizeof(uint32_t),
			 x, y, i);
	}

	for (i = 0; i < num_total_tiles; i++)
		tile_permutation[i] = i;
}

static void fan_in_and_check(void)
{
	uint32_t tmp_tile[TILE_SIZE*TILE_SIZE];
	unsigned tile, buf_idx, x, y;
	int i;
	for (i = 0; i < num_total_tiles; i++) {
		tile = tile_permutation[i];
		buf_idx = tile / TILES_PER_BUF;
		tile %= TILES_PER_BUF;

		tile2xy(&buffers[current_set][buf_idx], tile, &x, &y);

		cpucpy2d(buffers[current_set][buf_idx].data,
			 buffers[current_set][buf_idx].stride / sizeof(uint32_t),
			 x, y,
			 tmp_tile, TILE_SIZE, 0, 0,
			 i);
	}
}

static void init_buffer(struct scratch_buf *buf, unsigned size)
{
	buf->bo = drm_intel_bo_alloc(bufmgr, "tiled bo", size, 4096);
	assert(buf->bo);
	buf->tiling = I915_TILING_NONE;
	buf->stride = 8192;

	if (options.no_hw)
		buf->data = malloc(size);
	else {
		drm_intel_gem_bo_map_gtt(buf->bo);
		buf->data = buf->bo->virtual;
	}

	buf->num_tiles = size / TILE_BYTES;
}

static void permute_array(void *array, unsigned size,
			  void (*exchange_func)(void *array, unsigned i, unsigned j))
{
	int i;
	long int l;

	for (i = size - 1; i > 1; i--) {
		l = random();
		l %= i+1; /* yes, no perfectly uniform, who cares */
		exchange_func(array, i, l);
	}
}

static void exchange_buf(void *array, unsigned i, unsigned j)
{
	struct scratch_buf *buf_arr, tmp;
	buf_arr = array;

	memcpy(&tmp, &buf_arr[i], sizeof(struct scratch_buf));
	memcpy(&buf_arr[i], &buf_arr[j], sizeof(struct scratch_buf));
	memcpy(&buf_arr[j], &tmp, sizeof(struct scratch_buf));
}


/* libdrm is to clever and prevents us from changin tiling of buffers already
 * used in relocations. */
static void set_tiling(drm_intel_bo *bo, unsigned *tiling, unsigned stride)
{
	struct drm_i915_gem_set_tiling set_tiling;
	int ret;

	memset(&set_tiling, 0, sizeof(set_tiling));
	do {
		/* set_tiling is slightly broken and overwrites the
		 * input on the error path, so we have to open code
		 * drmIoctl.
		 */
		set_tiling.handle = bo->handle;
		set_tiling.tiling_mode = *tiling;
		set_tiling.stride = tiling ? stride : 0;

		ret = ioctl(drm_fd,
			    DRM_IOCTL_I915_GEM_SET_TILING,
			    &set_tiling);
	} while (ret == -1 && (errno == EINTR || errno == EAGAIN));
	assert(ret != -1);

	*tiling = set_tiling.tiling_mode;
}

static void init_set(unsigned set)
{
	long int r;
	int i;

	permute_array(buffers[set], num_buffers, exchange_buf);

	if (current_set == 1 && options.gpu_busy_load == 0) {
		gpu_busy_load++;
		if (gpu_busy_load > 10)
			gpu_busy_load = 6;
	}

	for (i = 0; i < num_buffers; i++) {
		r = random();
		if ((r & 3) != 0)
		    continue;
		r >>= 2;

		if ((r & 3) != 0)
			buffers[set][i].tiling = I915_TILING_X;
		else
			buffers[set][i].tiling = I915_TILING_NONE;
		r >>= 2;
		if (options.no_tiling)
			buffers[set][i].tiling = I915_TILING_NONE;

		if (buffers[set][i].tiling == I915_TILING_NONE) {
			/* min 64 byte stride */
			r %= 8;
			buffers[set][i].stride = 64 * (1 << r);
		} else if (IS_GEN2(devid)) {
			/* min 128 byte stride */
			r %= 7;
			buffers[set][i].stride = 128 * (1 << r);
		} else {
			/* min 512 byte stride */
			r %= 5;
			buffers[set][i].stride = 512 * (1 << r);
		}
		assert(buffers[set][i].stride <= 8192);

		set_tiling(buffers[set][i].bo,
			   &buffers[set][i].tiling,
			   buffers[set][i].stride);

		if (i == options.trace_tile/TILES_PER_BUF)
			printf("changing buffer %i containing tile %i: tiling %i, stride %i\n", i, 
					options.trace_tile,
					buffers[set][i].tiling, buffers[set][i].stride);
	}
}

static void exchange_uint(void *array, unsigned i, unsigned j)
{
	unsigned *i_arr = array;
	unsigned i_tmp;

	i_tmp = i_arr[i];
	i_arr[i] = i_arr[j];
	i_arr[j] = i_tmp;
}

static void copy_tiles(unsigned *permutation)
{
	unsigned src_tile, src_buf_idx, src_x, src_y;
	unsigned dst_tile, dst_buf_idx, dst_x, dst_y;
	struct scratch_buf *src_buf, *dst_buf;
	int i, idx;
	for (i = 0; i < num_total_tiles; i++) {
		/* tile_permutation is independant of current_permutation, so
		 * abuse it to randomize the order of the src bos */
		idx  = tile_permutation[i];
		src_buf_idx = idx / TILES_PER_BUF;
		src_tile = idx % TILES_PER_BUF;
		src_buf = &buffers[current_set][src_buf_idx];

		tile2xy(src_buf, src_tile, &src_x, &src_y);

		dst_buf_idx = permutation[idx] / TILES_PER_BUF;
		dst_tile = permutation[idx] % TILES_PER_BUF;
		dst_buf = &buffers[target_set][dst_buf_idx];

		tile2xy(dst_buf, dst_tile, &dst_x, &dst_y);

		if (options.trace_tile == i)
			printf("copying tile %i from %i (%i, %i) to %i (%i, %i)", i,
				tile_permutation[i], src_buf_idx, src_tile,
				permutation[idx], dst_buf_idx, dst_tile);

		if (options.no_hw) {
			cpucpy2d(src_buf->data,
				 src_buf->stride / sizeof(uint32_t),
				 src_x, src_y,
				 dst_buf->data,
				 dst_buf->stride / sizeof(uint32_t),
				 dst_x, dst_y,
				 i);
		} else {
			next_copyfunc(i);

			copyfunc(src_buf, src_x, src_y, dst_buf, dst_x, dst_y,
				 i);
		}
	}

	intel_batchbuffer_flush(batch);
}

static int get_num_fences(void)
{
	drm_i915_getparam_t gp;
	int ret, val;

	gp.param = I915_PARAM_NUM_FENCES_AVAIL;
	gp.value = &val;
	ret = drmIoctl(drm_fd, DRM_IOCTL_I915_GETPARAM, &gp);
	assert (ret == 0);

	printf ("total %d fences\n", val);
	assert(val > 4);

	return val - 2;
}

static void parse_options(int argc, char **argv)
{
	int c, tmp;
	int option_index = 0;
	static struct option long_options[] = {
		{"no-hw", 0, 0, 'd'},
		{"buf-size", 1, 0, 's'},
		{"gpu-busy-load", 1, 0, 'g'},
		{"buffer-count", 1, 0, 'c'},
		{"trace-tile", 1, 0, 't'},
		{"disable-render", 0, 0, 'r'},
		{"untiled", 0, 0, 'u'}
	};

	options.scratch_buf_size = 256*4096;
	options.no_hw = 0;
	options.gpu_busy_load = 0;
	options.num_buffers = 0;
	options.trace_tile = -1;
	options.use_render = 1;
	options.no_tiling = 0;

	while((c = getopt_long(argc, argv, "ns:g:c:t:ru",
			       long_options, &option_index)) != -1) {
		switch(c) {
		case 'd':
			options.no_hw = 1;
			printf("no-hw debug mode\n");
			break;
		case 's':
			tmp = atoi(optarg);
			if (tmp < TILE_SIZE*8192)
				printf("scratch buffer size needs to be at least %i\n",
				       TILE_SIZE*8192);
			else if (tmp & (tmp - 1)) {
				printf("scratch buffer size needs to be a power-of-two\n");
			} else {
				printf("fixed scratch buffer size to %u\n", tmp);
				options.scratch_buf_size = tmp;
			}
			break;
		case 'g':
			tmp = atoi(optarg);
			if (tmp < 0 || tmp > 10)
				printf("gpu busy load needs to be bigger than 0 and smaller than 10\n");
			else {
				printf("gpu busy load factor set to %i\n", tmp);
				gpu_busy_load = options.gpu_busy_load = tmp;
			}
			break;
		case 'c':
			options.num_buffers = atoi(optarg);
			printf("buffer count set to %i\n", options.num_buffers);
			break;
		case 't':
			options.trace_tile = atoi(optarg);
			printf("tracing tile %i\n", options.trace_tile);
			break;
		case 'r':
			options.use_render = 0;
			printf("disabling render copy\n");
			break;
		case 'u':
			options.no_tiling = 1;
			printf("disabling tiling\n");
			break;
		default:
			printf("unkown command options\n");
			break;
		}
	}

	if (optind < argc)
		printf("unkown command options\n");
}

static void init(void)
{
	int i;
	unsigned tmp;

	drm_fd = drm_open_any();
	if (options.num_buffers == 0) {
		tmp = gem_aperture_size(drm_fd);
		tmp = tmp > 256*(1024*1024) ? 256*(1024*1024) : tmp;
		num_buffers = 2 * tmp / options.scratch_buf_size / 3;
		num_buffers /= 2;
		printf("using %u buffers\n", num_buffers);
	} else
		num_buffers = options.num_buffers;

	bufmgr = drm_intel_bufmgr_gem_init(drm_fd, 4096);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);
	drm_intel_bufmgr_gem_enable_fenced_relocs(bufmgr);
	devid = intel_get_drm_devid(drm_fd);
	num_fences = get_num_fences();
	batch = intel_batchbuffer_alloc(bufmgr, devid);
	busy_bo = drm_intel_bo_alloc(bufmgr, "tiled bo", BUSY_BUF_SIZE, 4096);

	for (i = 0; i < num_buffers; i++) {
		init_buffer(&buffers[0][i], options.scratch_buf_size);
		init_buffer(&buffers[1][i], options.scratch_buf_size);

		num_total_tiles += buffers[0][i].num_tiles;
	}
	current_set = 0;

	/* just in case it helps reproducability */
	srandom(0xdeadbeef);
}

int main(int argc, char **argv)
{
	int i, j;
	unsigned *current_permutation, *tmp_permutation;

	parse_options(argc, argv);

	init();

	tile_permutation = malloc(num_total_tiles*sizeof(uint32_t));
	current_permutation = malloc(num_total_tiles*sizeof(uint32_t));
	tmp_permutation = malloc(num_total_tiles*sizeof(uint32_t));
	assert(tile_permutation);
	assert(current_permutation);
	assert(tmp_permutation);

	fan_out();

	for (i = 0; i < 512; i++) {
		printf("round %i\n", i);
		if (i % 64 == 63) {
			fan_in_and_check();
			printf("everything correct after %i rounds\n", i + 1);
		}

		target_set = (current_set + 1) & 1;
		init_set(target_set);

		for (j = 0; j < num_total_tiles; j++)
			current_permutation[j] = j;
		permute_array(current_permutation, num_total_tiles, exchange_uint);

		copy_tiles(current_permutation);

		memcpy(tmp_permutation, tile_permutation, sizeof(unsigned)*num_total_tiles);

		/* accumulate the permutations */
		for (j = 0; j < num_total_tiles; j++)
			tile_permutation[j] = current_permutation[tmp_permutation[j]];

		current_set = target_set;
	}

	fan_in_and_check();

	intel_batchbuffer_free(batch);
	drm_intel_bufmgr_destroy(bufmgr);

	close(drm_fd);

	return 0;
}
