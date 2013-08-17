#define MAX_RINGS 4

#include <stdint.h>

struct gpu_top {
	int fd;
	int num_rings;
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
};

void gpu_top_init(struct gpu_top *gt);
int gpu_top_update(struct gpu_top *gt);
