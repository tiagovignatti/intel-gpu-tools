#include <stdint.h>

struct power {
	struct power_stat {
		uint64_t energy;
		uint64_t timestamp;
	} stat[2];

	int error;
	int count;
	int new_sample;

	uint64_t power_mW;
};

int power_init(struct power *power);
int power_update(struct power *power);
