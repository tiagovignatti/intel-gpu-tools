#ifndef INTEL_L3_PARITY_H_
#define INTEL_L3_PARITY_H_

#include <stdint.h>
#include <stdbool.h>

struct l3_parity {
	struct udev *udev;
	struct udev_monitor *uevent_monitor;
	int fd;
	fd_set fdset;
};

struct l3_location {
	uint8_t slice;
	uint16_t row;
	uint8_t bank;
	uint8_t subbank;
};

#if HAVE_UDEV
int l3_uevent_setup(struct l3_parity *par);
/* Listens (blocks) for an l3 parity event. Returns the location of the error. */
int l3_listen(struct l3_parity *par, bool daemon, struct l3_location *loc);
#define l3_uevent_teardown(par) {}
#else
#define l3_uevent_setup(par, daemon, loc) -1
#define l3_listen(par) -1
#endif

#endif
