#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "igfx.h"
#include "gpu-top.h"

#define RING_TAIL      0x00
#define RING_HEAD      0x04
#define ADDR_MASK      0x001FFFFC
#define RING_CTL       0x0C
#define   RING_WAIT		(1<<11)
#define   RING_WAIT_SEMAPHORE	(1<<10)

struct ring {
	int id;
	uint32_t mmio;
	int idle, wait, sema;
};

static void *mmio;

static uint32_t ring_read(struct ring *ring, uint32_t reg)
{
	return igfx_read(mmio, ring->mmio + reg);
}

static void ring_init(struct ring *ring)
{
	uint32_t ctl;

	ctl = ring_read(ring, RING_CTL);
	if ((ctl & 1) == 0)
		ring->id = -1;
}

static void ring_reset(struct ring *ring)
{
	ring->idle = 0;
	ring->wait = 0;
	ring->sema = 0;
}

static void ring_sample(struct ring *ring)
{
	uint32_t head, tail, ctl;

	if (ring->id == -1)
		return;

	head = ring_read(ring, RING_HEAD) & ADDR_MASK;
	tail = ring_read(ring, RING_TAIL) & ADDR_MASK;
	ring->idle += head == tail;

	ctl = ring_read(ring, RING_CTL);
	ring->wait += !!(ctl & RING_WAIT);
	ring->sema += !!(ctl & RING_WAIT_SEMAPHORE);
}

static void ring_emit(struct ring *ring, int samples, union gpu_top_payload *payload)
{
	if (ring->id == -1)
		return;

	payload[ring->id].u.busy = 100 - 100 * ring->idle / samples;
	payload[ring->id].u.wait = 100 * ring->wait / samples;
	payload[ring->id].u.sema = 100 * ring->sema / samples;
}

void gpu_top_init(struct gpu_top *gt)
{
	struct ring render_ring = {
		.mmio = 0x2030,
		.id = 0,
	}, bsd_ring = {
		.mmio = 0x4030,
		.id = 1,
	}, bsd6_ring = {
		.mmio = 0x12030,
		.id = 1,
	}, blt_ring = {
		.mmio = 0x22030,
		.id = 2,
	};
	const struct igfx_info *info;
	struct pci_device *igfx;
	int fd[2], i;

	memset(gt, 0, sizeof(*gt));
	gt->fd = -1;

	igfx = igfx_get();
	if (!igfx)
		return;

	if (pipe(fd) < 0)
		return;

	info = igfx_get_info(igfx);

	switch (fork()) {
	case -1: return;
	default:
		 fcntl(fd[0], F_SETFL, fcntl(fd[0], F_GETFL) | O_NONBLOCK);
		 gt->fd = fd[0];
		 gt->ring[0].name = "render";
		 gt->num_rings = 1;
		 if (info->gen >= 040) {
			 gt->ring[1].name = "bitstream";
			 gt->num_rings++;
		 }
		 if (info->gen >= 060) {
			 gt->ring[2].name = "blt";
			 gt->num_rings++;
		 }
		 close(fd[1]);
		 return;
	case 0:
		 close(fd[0]);
		 break;
	}

	mmio = igfx_get_mmio(igfx);

	ring_init(&render_ring);
	if (info->gen >= 060) {
		ring_init(&bsd6_ring);
		ring_init(&blt_ring);
	} else if (info->gen >= 040) {
		ring_init(&bsd_ring);
	}

	for (;;) {
		union gpu_top_payload payload[MAX_RINGS];

		ring_reset(&render_ring);
		ring_reset(&bsd_ring);
		ring_reset(&bsd6_ring);
		ring_reset(&blt_ring);

		for (i = 0; i < 1000; i++) {
			ring_sample(&render_ring);
			ring_sample(&bsd_ring);
			ring_sample(&bsd6_ring);
			ring_sample(&blt_ring);
			usleep(1000);
		}

		ring_emit(&render_ring, 1000, payload);
		ring_emit(&bsd_ring, 1000, payload);
		ring_emit(&bsd6_ring, 1000, payload);
		ring_emit(&blt_ring, 1000, payload);

		write(fd[1], payload, sizeof(payload));
	}
}

int gpu_top_update(struct gpu_top *gt)
{
	uint32_t data[1024];
	int len, update = 0;

	if (gt->fd < 0)
		return update;

	while ((len = read(gt->fd, data, sizeof(data))) > 0) {
		uint32_t *ptr = &data[len/sizeof(uint32_t) - MAX_RINGS];
		gt->ring[0].u.payload = ptr[0];
		gt->ring[1].u.payload = ptr[1];
		gt->ring[2].u.payload = ptr[2];
		gt->ring[3].u.payload = ptr[3];
		update = 1;
	}

	return update;
}
