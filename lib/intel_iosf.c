#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <errno.h>
#include "intel_gpu_tools.h"

#define TIMEOUT_US 500000

static int vlv_punit_rw(uint32_t port, uint8_t opcode, uint8_t addr,
			uint32_t *val)
{
	volatile uint32_t *ptr;
	int timeout = 0;
	uint32_t cmd, devfn, be, bar;

	bar = 0;
	be = 0xf;
	devfn = 16;

	cmd = (devfn << IOSF_DEVFN_SHIFT) | (opcode << IOSF_OPCODE_SHIFT) |
		(port << IOSF_PORT_SHIFT) | (be << IOSF_BYTE_ENABLES_SHIFT) |
		(bar << IOSF_BAR_SHIFT);

	ptr = (volatile uint32_t*)((volatile char*)mmio +
				   VLV_IOSF_DOORBELL_REQ);

	if (*ptr & IOSF_SB_BUSY) {
		fprintf(stderr, "warning: pcode (%s) mailbox access failed\n",
			opcode == PUNIT_OPCODE_REG_READ ?
			"read" : "write");
		return -EAGAIN;
	}

	ptr = (volatile uint32_t*)((volatile char*)mmio + VLV_IOSF_ADDR);
	*ptr = addr;
	if (opcode == PUNIT_OPCODE_REG_WRITE) {
		ptr = (volatile uint32_t*)((volatile char*)mmio +
					   VLV_IOSF_DATA);
		*ptr = *val;
	}
	ptr = (volatile uint32_t*)((volatile char*)mmio +
				   VLV_IOSF_DOORBELL_REQ);
	*ptr = cmd;
	do {
		usleep(1);
		timeout++;
	} while ((*ptr & IOSF_SB_BUSY) && timeout < TIMEOUT_US);

	if (timeout >= TIMEOUT_US) {
		fprintf(stderr, "timeout waiting for pcode %s (%d) to finish\n",
			opcode == PUNIT_OPCODE_REG_READ ? "read" : "write",
			addr);
		return -ETIMEDOUT;
	}

	if (opcode == PUNIT_OPCODE_REG_READ) {
		ptr = (volatile uint32_t*)((volatile char*)mmio +
					   VLV_IOSF_DATA);
		*val = *ptr;
	}
	*ptr = 0;

	return 0;
}

int intel_punit_read(uint8_t addr, uint32_t *val)
{
	return vlv_punit_rw(IOSF_PORT_PUNIT, PUNIT_OPCODE_REG_READ, addr, val);
}

int intel_punit_write(uint8_t addr, uint32_t val)
{
	return vlv_punit_rw(IOSF_PORT_PUNIT, PUNIT_OPCODE_REG_WRITE, addr, &val);
}

int intel_nc_read(uint8_t addr, uint32_t *val)
{
	return vlv_punit_rw(IOSF_PORT_NC, PUNIT_OPCODE_REG_READ, addr, val);
}

int intel_nc_write(uint8_t addr, uint32_t val)
{
	return vlv_punit_rw(IOSF_PORT_NC, PUNIT_OPCODE_REG_WRITE, addr, &val);
}
