/*
 * Copyright © 2013 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if HAVE_UDEV
#include <libudev.h>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <syslog.h>
#include "i915_drm.h"
#include "intel_l3_parity.h"

#ifndef I915_L3_PARITY_UEVENT
#define I915_L3_PARITY_UEVENT "L3_PARITY_ERROR"
#endif

int l3_uevent_setup(struct l3_parity *par)
{
	struct udev *udev;
	struct udev_monitor *uevent_monitor;
	fd_set fdset;
	int fd, ret = -1;

	udev = udev_new();
	if (!udev) {
		return -1;
	}

	uevent_monitor = udev_monitor_new_from_netlink(udev, "udev");
	if (!uevent_monitor)
		goto err_out;

	ret = udev_monitor_filter_add_match_subsystem_devtype(uevent_monitor, "drm", "drm_minor");
	if (ret < 0)
		goto err_out;

	ret = udev_monitor_enable_receiving(uevent_monitor);
	if (ret < 0)
		goto err_out;

	fd = udev_monitor_get_fd(uevent_monitor);
	FD_ZERO(&fdset);
	FD_SET(fd, &fdset);

	par->udev = udev;
	par->fd = fd;
	par->fdset = fdset;
	par->uevent_monitor = uevent_monitor;
	return 0;

err_out:
	udev_unref(udev);
	return ret;
}

int l3_listen(struct l3_parity *par, bool daemon, struct l3_location *loc)
{
	struct udev_device *udev_dev;
	const char *parity_status;
	char *err_msg;
	int ret;

again:
	ret = select(par->fd + 1, &par->fdset, NULL, NULL, NULL);
	/* Number of bits set is returned, must be >= 1 */
	if (ret <= 0) {
		return ret;
	}

	assert(FD_ISSET(par->fd, &par->fdset));

	udev_dev = udev_monitor_receive_device(par->uevent_monitor);
	if (!udev_dev)
		return -1;

	parity_status = udev_device_get_property_value(udev_dev, I915_L3_PARITY_UEVENT);
	if (strncmp(parity_status, "1", 1))
		goto again;

	loc->slice = atoi(udev_device_get_property_value(udev_dev, "SLICE"));
	loc->row = atoi(udev_device_get_property_value(udev_dev, "ROW"));
	loc->bank = atoi(udev_device_get_property_value(udev_dev, "BANK"));
	loc->subbank = atoi(udev_device_get_property_value(udev_dev, "SUBBANK"));

	udev_device_unref(udev_dev);

	assert(asprintf(&err_msg, "Parity error detected on: %d,%d,%d,%d. "
			"Try to run intel_l3_parity -r %d -b %d -s %d -w %d -d",
			loc->slice, loc->row, loc->bank, loc->subbank,
			loc->row, loc->bank, loc->subbank, loc->slice) != -1);
	if (daemon) {
		syslog(LOG_INFO, "%s\n", err_msg);
		goto again;
	}

	fprintf(stderr, "%s\n", err_msg);

	free(err_msg);

	return 0;
}
#endif
