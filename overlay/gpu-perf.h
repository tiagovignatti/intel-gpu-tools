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

	int flip_complete[4];
	struct gpu_perf_comm {
		struct gpu_perf_comm *next;
		char name[256];
		pid_t pid;
		int nr_requests[4];
		void *user_data;

		uint64_t wait_time;
		uint64_t busy_time;
	} *comm;
	struct gpu_perf_time {
		struct gpu_perf_time *next;
		struct gpu_perf_comm *comm;
		int ring;
		uint32_t seqno;
		uint64_t time;
	} *wait, *busy;
};

void gpu_perf_init(struct gpu_perf *gp, unsigned flags);
int gpu_perf_update(struct gpu_perf *gp);
