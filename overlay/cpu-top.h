#include <stdint.h>

struct cpu_top {
	uint8_t busy;

	int count;
	struct cpu_stat {
		uint64_t user, nice, sys, idle;
		uint64_t total;
	} stat[2];
};

int cpu_top_update(struct cpu_top *cpu);
