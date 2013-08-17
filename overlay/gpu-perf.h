#include <stdint.h>

struct gpu_perf {
	int page_size;
	int nr_cpus;
	int nr_events;
	int *fd;
	void **map;
	struct gpu_perf_sample {
		uint64_t id;
		int (*func)(struct gpu_perf *, const void *);
	} *sample;

	int flip_complete;
};

void gpu_perf_init(struct gpu_perf *gp, unsigned flags);
int gpu_perf_update(struct gpu_perf *gp);
