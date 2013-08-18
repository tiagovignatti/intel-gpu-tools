#include <stdint.h>

struct gem_objects {
	uint64_t total_bytes;
	uint32_t total_count;
	uint64_t total_gtt, total_aperture;
	uint64_t max_gtt, max_aperture;
	struct gem_objects_comm {
		struct gem_objects_comm *next;
		char name[256];
		uint64_t bytes;
		uint32_t count;
	} *comm;
};

int gem_objects_init(struct gem_objects *obj);
int gem_objects_update(struct gem_objects *obj);
