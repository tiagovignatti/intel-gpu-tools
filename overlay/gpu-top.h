#define MAX_RINGS 4

#include <stdint.h>

struct gpu_top {
	enum { PERF, MMIO } type;
	int fd;

	int num_rings;
	int have_wait;
	int have_sema;

	struct gpu_top_ring {
		const char *name;
		union gpu_top_payload {
			struct {
				uint8_t busy;
				uint8_t wait;
				uint8_t sema;
			} u;
			uint32_t payload;
		} u;
	} ring[MAX_RINGS];

	struct gpu_top_stat {
		uint64_t time;
		uint64_t busy[MAX_RINGS];
		uint64_t wait[MAX_RINGS];
		uint64_t sema[MAX_RINGS];
	} stat[2];
	int count;
};

void gpu_top_init(struct gpu_top *gt);
int gpu_top_update(struct gpu_top *gt);
