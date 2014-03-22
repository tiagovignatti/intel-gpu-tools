/*
 * Copyright © 2011 Intel Corporation
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
 *    Ben Widawsky <ben@bwidawsk.net>
 *
 * Notes:
 *
 */

#include <signal.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/un.h>
#include <sys/socket.h>
#include "drm.h"
#include "i915_drm.h"
#include "drmtest.h"
#include "intel_chipset.h"
#include "intel_bufmgr.h"
#include "intel_io.h"
#include "intel_batchbuffer.h"
#include "intel_debug.h"
#include "debug.h"

#define EU_ATT		0x7810
#define EU_ATT_CLR	0x7830

#define RSVD_EU -1
#define RSVD_THREAD -1
#define RSVD_ID EUID(-1, -1, -1)

enum {
	EBAD_SHMEM,
	EBAD_PROTOCOL,
	EBAD_MAGIC,
	EBAD_WRITE
};

struct debuggee {
	int euid;
	int tid;
	int fd;
	int clr;
	uint32_t reg;
};

struct debugger {
	struct debuggee *debuggees;
	int num_threads;
	int real_num_threads;
	int threads_per_eu;
} *eu_info;

drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;
drm_intel_bo *scratch_bo;

int handle;
int drm_fd;
int debug_fd = 0;
const char *debug_file = "dump_debug.bin";
int debug;
int clear_waits;
int shutting_down = 0;
struct intel_debug_handshake dh;
int force_clear = 0;
uint32_t old_td_ctl;

/*
 * The docs are wrong about the attention clear bits. The clear bits are
 * provided as part of the structure in case they change in future generations.
 */
#define EUID(eu, td, clear) \
	{ .euid = eu, .tid = td, .reg = EU_ATT, .fd = -1, .clr = clear }
#define EUID2(eu, td, clear) \
	{ .euid = eu, .tid = td, .reg = EU_ATT + 4, .fd = -1, .clr = clear }
struct debuggee gt1_debug_ids[] = {
	RSVD_ID, RSVD_ID,
	RSVD_ID, EUID(6, 3, 28), EUID(6, 2, 27), EUID(6, 1, 26), EUID(6, 0, 25),
	RSVD_ID, EUID(5, 3, 23), EUID(5, 2, 22), EUID(5, 1, 21), EUID(5, 0, 20),
	RSVD_ID, EUID(4, 3, 18), EUID(4, 2, 17), EUID(4, 1, 16), EUID(4, 0, 15),
	RSVD_ID, EUID(2, 3, 13), EUID(2, 2, 12), EUID(2, 1, 11), EUID(2, 0, 10),
	RSVD_ID, EUID(1, 3, 8), EUID(1, 2, 7), EUID(1, 1, 6), EUID(1, 0, 5),
	RSVD_ID, EUID(0, 3, 3), EUID(0, 2, 2), EUID(0, 1, 1), EUID(0, 0, 0)
};

struct debuggee gt2_debug_ids[] = {
	EUID(8, 1, 31), EUID(8, 0, 30),
	EUID(6, 4, 29), EUID(6, 3, 28), EUID(6, 2, 27), EUID(6, 1, 26), EUID(6, 0, 25),
	EUID(5, 4, 24), EUID(5, 3, 23), EUID(5, 2, 22), EUID(5, 1, 21), EUID(5, 0, 20),
	EUID(4, 4, 19), EUID(4, 3, 18), EUID(4, 2, 17), EUID(4, 1, 16), EUID(4, 0, 15),
	EUID(2, 4, 14), EUID(2, 3, 13), EUID(2, 2, 12), EUID(2, 1, 11), EUID(2, 0, 10),
	EUID(1, 4, 9), EUID(1, 3, 8), EUID(1, 2, 7), EUID(1, 1, 6), EUID(1, 0, 5),
	EUID(0, 4, 4), EUID(0, 3, 3), EUID(0, 2, 2), EUID(0, 1, 1), EUID(0, 0, 0),
	RSVD_ID, RSVD_ID, RSVD_ID, RSVD_ID,
	EUID2(14, 4, 27), EUID2(14, 3, 26), EUID2(14, 2, 25), EUID2(14, 1, 24), EUID2(14, 0, 23),
	EUID2(13, 4, 22), EUID2(13, 3, 21), EUID2(13, 2, 20), EUID2(13, 1, 19), EUID2(13, 0, 18),
	EUID2(12, 4, 17), EUID2(12, 3, 16), EUID2(12, 2, 15), EUID2(12, 1, 14), EUID2(12, 0, 13),
	EUID2(10, 4, 12), EUID2(10, 3, 11), EUID2(10, 2, 10), EUID2(10, 1, 9), EUID2(10, 0, 8),
	EUID2(9, 4, 7), EUID2(9, 3, 6), EUID2(9, 2, 5), EUID2(9, 1, 4), EUID2(9, 0, 3),
	EUID2(8, 4, 2), EUID2(8, 3, 1), EUID2(8, 2, 0)
};

struct debugger gt1 = {
	.debuggees = gt1_debug_ids,
	.num_threads = 32,
	.real_num_threads = 24,
	.threads_per_eu = 4
};

struct debugger gt2 = {
	.debuggees = gt2_debug_ids,
	.num_threads = 64,
	.real_num_threads = 60,
	.threads_per_eu = 5
};

static void
dump_debug(void *buf, size_t count) {
	if (!debug_fd)
		debug_fd = open(debug_file, O_CREAT | O_WRONLY | O_TRUNC, S_IRWXO);

	write(debug_fd, buf, count);
}

static volatile void *
map_debug_buffer(void) {
	int ret;

	ret = drm_intel_bo_map(scratch_bo, 0);
	assert(ret == 0);
	return scratch_bo->virtual;
}

static void
unmap_debug_buffer(void) {
	drm_intel_bo_unmap(scratch_bo);
}

static int
wait_for_attn(int timeout, int *out_bits) {
	int step = 1;
	int eus_waiting = 0;
	int i,j;

	if (timeout <= 0) {
		timeout = 1;
		step = 0;
	}

	for (i = 0; i < timeout; i += step) {
		for (j = 0; j < 8; j += 4) {
			uint32_t attn = intel_register_read(EU_ATT + j);
			if (attn) {
				int bit = 0;
				while( (bit = ffs(attn)) != 0) {
					bit--; // ffs is 1 based
					assert(bit >= 0);
					out_bits[eus_waiting] = bit + (j * 8);
					attn &= ~(1 << bit);
					eus_waiting++;
				}
			}
		}

		if (intel_register_read(EU_ATT + 8) ||
		    intel_register_read(EU_ATT + 0xc)) {
			fprintf(stderr, "Unknown attention bits\n");
		}

		if (eus_waiting || shutting_down)
			break;
	}

	return eus_waiting;
}

#define eu_fd(bit) eu_info->debuggees[bit].fd
#define eu_id(bit) eu_info->debuggees[bit].euid
#define eu_tid(bit) eu_info->debuggees[bit].tid
static struct eu_state *
find_eu_shmem(int bit, volatile uint8_t *buf) {
	struct per_thread_data {
		uint8_t ____[dh.per_thread_scratch];
	}__attribute__((packed)) *data;
	struct eu_state *eu;
	int mem_tid, mem_euid, i;

	data = (struct per_thread_data *)buf;
	for(i = 0; i < eu_info->num_threads; i++) {
		eu = (struct eu_state *)&data[i];
		mem_tid = eu->sr0 & 0x7;
		mem_euid = (eu->sr0 >> 8) & 0xf;
		if (mem_tid == eu_tid(bit) && mem_euid == eu_id(bit))
			break;
		eu = NULL;
	}

	return eu;
}

#define GRF_CMP(a, b) memcmp(a, b, sizeof(grf))
#define GRF_CPY(a, b) memcpy(a, b, sizeof(grf))
static int
verify(struct eu_state *eu) {
	if (GRF_CMP(eu->version, protocol_version)) {
		if (debug) {
			printf("Bad EU protocol version %x %x\n",
				((uint32_t *)&eu->version)[0],
				DEBUG_PROTOCOL_VERSION);
			dump_debug((void *)eu, sizeof(*eu));
		}
		return -EBAD_PROTOCOL;
	}

	if (GRF_CMP(eu->state_magic, eu_msg)) {
		if (debug) {
			printf("Bad EU state magic %x %x\n",
				((uint32_t *)&eu->state_magic)[0],
				((uint32_t *)&eu->state_magic)[1]);
			dump_debug((void *)eu, sizeof(*eu));
		}
		return -EBAD_MAGIC;
	} else {
		GRF_CPY(eu->state_magic, cpu_ack);
	}

	eu->sr0 = RSVD_EU << 8 | RSVD_THREAD;
	return 0;
}

static int
collect_data(int bit, volatile uint8_t *buf) {
	struct eu_state *eu;
	ssize_t num;
	int ret;

	assert(eu_id(bit) != RSVD_EU);

	if (eu_fd(bit) == -1) {
		char name[128];
		sprintf(name, "dump_eu_%02d_%d.bin", eu_id(bit), eu_tid(bit));
		eu_fd(bit) = open(name, O_CREAT | O_WRONLY | O_TRUNC, S_IRWXO);
		if (eu_fd(bit) == -1)
			return -1;
	}

	eu = find_eu_shmem(bit, buf);

	if (eu == NULL) {
		if (debug)
			printf("Bad offset %d %d\n", eu_id(bit), eu_tid(bit));
		return -EBAD_SHMEM;
	}

	ret = verify(eu);
	if (ret)
		return ret;

	num = write(eu_fd(bit), (void *)eu, sizeof(*eu));
	if (num != sizeof(*eu)) {
		perror("unhandled write failure");
		return EBAD_WRITE;
	}


	return 0;
}

static void
clear_attn(int bit) {
#if 0
/*
 * This works but doesn't allow for easily changed clearing bits
 */
static void
clear_attn_old(int bit) {
	int bit_to_clear = bit % 32;
	bit_to_clear = 31 - bit_to_clear;
	intel_register_write(0x7830 + (bit/32) * 4, 0);
	intel_register_write(0x7830 + (bit/32) * 4, 1 << bit_to_clear);
}
#else
	if (!force_clear) {
		int bit_to_clear;
		bit_to_clear = eu_info->debuggees[bit].clr;
		intel_register_write(EU_ATT_CLR + (bit/32) * 4, 0);
		intel_register_write(EU_ATT_CLR + (bit/32) * 4, 1 << bit_to_clear);
	} else {
		intel_register_write(EU_ATT_CLR + 0, 0);
		intel_register_write(EU_ATT_CLR + 4, 0);
		intel_register_write(EU_ATT_CLR + 0, 0xffffffff);
		intel_register_write(EU_ATT_CLR + 4, 0xffffffff);
	}
#endif
}

static void
db_shutdown(int sig) {
	shutting_down = 1;
	printf("Shutting down...\n");
}

static void
die(int reason) {
	int i = 0;

	intel_register_write(EU_ATT_CLR, 0);
	intel_register_write(EU_ATT_CLR + 4, 0);

	if (debug_fd)
		close(debug_fd);

	for (i = 0; i < eu_info->num_threads; i++) {
		if (eu_info->debuggees[i].fd != -1)
			close(eu_info->debuggees[i].fd);
	}

	unmap_debug_buffer();

	if (old_td_ctl)
		intel_register_write(TD_CTL, old_td_ctl);
	intel_register_access_fini();
	exit(reason);
}

static int
identify_device(int devid) {
	switch(devid) {
	case PCI_CHIP_SANDYBRIDGE_GT1:
	case PCI_CHIP_SANDYBRIDGE_M_GT1:
	case PCI_CHIP_SANDYBRIDGE_S:
		eu_info = &gt1;
		break;
	case PCI_CHIP_SANDYBRIDGE_GT2:
	case PCI_CHIP_SANDYBRIDGE_GT2_PLUS:
	case PCI_CHIP_SANDYBRIDGE_M_GT2:
	case PCI_CHIP_SANDYBRIDGE_M_GT2_PLUS:
		eu_info = &gt2;
		break;
	default:
		return 1;
	}

	return 0;
}

static void
parse_data(const char *file_name) {
	struct eu_state *eu_state = NULL;
	struct stat st;
	int fd = -1;
	int ret, i, elements;

	fd = open(file_name, O_RDONLY);
	if (fd == -1) {
		perror("open");
		goto out;
	}

	ret = fstat(fd, &st);
	if (ret == -1) {
		perror("fstat");
		goto out;
	}

	elements = st.st_size / sizeof(struct eu_state);
	if (elements == 0) {
		fprintf(stderr, "File not big enough for 1 entry\n");
		goto out;
	}

	eu_state = mmap(0, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (eu_state == MAP_FAILED) {
		perror("mmap");
		goto out;
	}

	for(i = 0; i < elements; i++) {
		printf("AIP: ");
			printf("%x\n", ((uint32_t *)eu_state[i].cr0)[2]);
	}
out:
	if (eu_state)
		munmap(eu_state, st.st_size);
	if (fd != -1)
		close(fd);
}

static int
wait_for_scratch_bo(void) {
	struct sockaddr_un addr;
	uint32_t version;
	int fd, ret, dh_handle = -1;

	assert(sizeof(version) == sizeof(dh.version));

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd == -1)
		return -1;

	/* Clean up previous runs */
	remove(SHADER_DEBUG_SOCKET);

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, SHADER_DEBUG_SOCKET, sizeof(addr.sun_path) - 1);

	ret = bind(fd, (const struct sockaddr *)&addr, sizeof(addr));
	if (ret == -1) {
		perror("listen");
		return -1;
	}

	ret = listen(fd, 1);
	if (ret == -1) {
		perror("listen");
		goto done;
	}

	while(1) {
		int client_fd;
		size_t count;
		char ack[] = DEBUG_HANDSHAKE_ACK;

		client_fd = accept(fd, NULL, NULL);
		if (client_fd == -1) {
			perror("accept");
			goto done;
		}

		count = read(client_fd, &version, sizeof(version));
		if (count != sizeof(version)) {
			perror("read version");
			goto loop_out;
		}

		if (version != DEBUG_HANDSHAKE_VERSION) {
			fprintf(stderr, "Bad debug handshake\n");
			goto loop_out;
		}

		count = read(client_fd, ((char *)&dh) + 1, sizeof(dh) - 1);
		if (count != sizeof(dh) - 1) {
			perror("read handshake");
			goto loop_out;
		}

		count = write(client_fd, ack, sizeof(ack));
		if (count != sizeof(ack)) {
			perror("write ack");
			goto loop_out;
		}
		dh_handle = dh.flink_handle;
		if (debug > 0) {
			printf("Handshake completed successfully\n"
				"\tprotocol version = %d\n"
				"\tflink handle = %d\n"
				"\tper thread scratch = %x\n", version,
				dh.flink_handle, dh.per_thread_scratch);
		}

	loop_out:
		close(client_fd);
		break;
	}

done:
	close(fd);
	return dh_handle;
}

static void
setup_hw_bits(void)
{
	intel_register_write(INST_PM, GEN6_GLOBAL_DEBUG_ENABLE |
				      GEN6_GLOBAL_DEBUG_ENABLE << 16);
	old_td_ctl = intel_register_read(GEN6_TD_CTL);
	intel_register_write(GEN6_TD_CTL, GEN6_TD_CTL_FORCE_TD_BKPT);
}

int main(int argc, char* argv[]) {
	struct pci_device *pci_dev;
	volatile uint8_t *scratch = NULL;
	int bits[64];
	int devid = -1, opt;

	while ((opt = getopt(argc, argv, "cdr:pf?h")) != -1) {
		switch (opt) {
		case 'c':
			clear_waits = 1;
			break;
		case 'd':
			debug = 1;
			break;
		case 'r':
			parse_data(optarg);
			exit(0);
			break;
		case 'p':
			devid = atoi(optarg);
			break;
		case 'f':
			force_clear  = 1;
			break;
		case '?':
		case 'h':
		default:
			exit(0);
		}
	}

	pci_dev = intel_get_pci_device();
	if (devid == -1)
		devid = pci_dev->device_id;
	if (identify_device(devid)) {
		abort();
	}

	assert(intel_register_access_init(pci_dev, 1) == 0);

	memset(bits, -1, sizeof(bits));
	/*
	 * These events have to occur before the SR runs, or we need
	 * non-blocking versions of the functions.
	 */
	if (!clear_waits) {
		int dh_handle;
		drm_fd = drm_open_any();
		bufmgr = drm_intel_bufmgr_gem_init(drm_fd, 4096);

		setup_hw_bits();

		/* We are probably root, make files world friendly */
		umask(0);
		dh_handle = wait_for_scratch_bo();
		if (dh_handle == -1) {
			printf("No handle from mesa, please enter manually: ");
			if (fscanf(stdin, "%1d", &dh_handle) == 0)
				exit(1);
		}
		scratch_bo = intel_bo_gem_create_from_name(bufmgr, "scratch", dh_handle);
		if (scratch_bo == NULL) {
			fprintf(stderr, "Couldn't flink buffer\n");
			abort();
		}
		signal(SIGINT, db_shutdown);
		printf("Press Ctrl-C to stop\n");
	} else {
		int time = force_clear ? 0 : 20000;
		while (wait_for_attn(time, bits)) {
			clear_attn(bits[0]);
			memset(bits, -1, sizeof(bits));
		}
		die(0);
	}

	scratch = map_debug_buffer();
	while (shutting_down == 0) {
		int num_events, i;

		memset(bits, -1, sizeof(bits));
		num_events = wait_for_attn(-1, bits);
		if (num_events == 0)
			break;

		for (i = 0; i < num_events; i++) {
			assert(bits[i] < 64 && bits[i] >= 0);
			if (collect_data(bits[i], scratch)) {
				bits[i] = -1;
				continue;
			}
			clear_attn(bits[i]);
		}
	}

	die(0);
	return 0;
}
