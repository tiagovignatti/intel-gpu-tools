#include <stdint.h>

struct rc6 {
	struct rc6_stat {
		uint64_t rc6_residency;
		uint64_t rc6p_residency;
		uint64_t rc6pp_residency;
		uint64_t timestamp;
	} stat[2];

	int count;
	int error;

	unsigned enabled;

	uint8_t rc6;
	uint8_t rc6p;
	uint8_t rc6pp;
	uint8_t rc6_combined;
};

int rc6_init(struct rc6 *rc6);
int rc6_update(struct rc6 *rc6);
