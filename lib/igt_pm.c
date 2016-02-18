/*
 * Copyright © 2013, 2015 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Paulo Zanoni <paulo.r.zanoni@intel.com>
 *    David Weinehall <david.weinehall@intel.com>
 *
 */
#include <fcntl.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "drmtest.h"
#include "igt_pm.h"

enum {
	POLICY_UNKNOWN = -1,
	POLICY_MAX_PERFORMANCE = 0,
	POLICY_MEDIUM_POWER = 1,
	POLICY_MIN_POWER = 2
};

#define MAX_PERFORMANCE_STR	"max_performance\n"
#define MEDIUM_POWER_STR	"medium_power\n"
#define MIN_POWER_STR		"min_power\n"
/* Remember to fix this if adding longer strings */
#define MAX_POLICY_STRLEN	strlen(MAX_PERFORMANCE_STR)

/**
 * SECTION:igt_pm
 * @short_description: Power Management related helpers
 * @title: Power Management
 * @include: igt.h
 *
 * This library provides various helpers to enable power management for,
 * and in some cases subsequently allow restoring the old behaviour of,
 * various external components that by default are set up in a way
 * that interferes with the testing of our power management functionality.
 */
/**
 * igt_pm_enable_audio_runtime_pm:
 *
 * We know that if we don't enable audio runtime PM, snd_hda_intel will never
 * release its power well refcount, and we'll never reach the LPSP state.
 * There's no guarantee that it will release the power well if we enable
 * runtime PM, but at least we can try.
 *
 * We don't have any assertions on open since the user may not even have
 * snd_hda_intel loaded, which is not a problem.
 */
void igt_pm_enable_audio_runtime_pm(void)
{
	int fd;

	fd = open("/sys/module/snd_hda_intel/parameters/power_save", O_WRONLY);
	if (fd >= 0) {
		igt_assert_eq(write(fd, "1\n", 2), 2);
		close(fd);
	}
	fd = open("/sys/bus/pci/devices/0000:00:03.0/power/control", O_WRONLY);
	if (fd >= 0) {
		igt_assert_eq(write(fd, "auto\n", 5), 5);
		close(fd);
	}
	/* Give some time for it to react. */
	sleep(1);
}

/**
 * igt_pm_enable_sata_link_power_management:
 *
 * Enable the min_power policy for SATA link power management.
 * Without this we cannot reach deep runtime power states.
 *
 * We don't have any assertions on open since the system might not have
 * a SATA host.
 *
 * Returns:
 * An opaque pointer to the data needed to restore the default values
 * after the test has terminated, or NULL if SATA link power management
 * is not supported. This pointer should be freed when no longer used
 * (typically after having called restore_sata_link_power_management()).
 */
int8_t *igt_pm_enable_sata_link_power_management(void)
{
	int fd, i;
	ssize_t len;
	char *buf;
	char *file_name;
	int8_t *link_pm_policies = NULL;

	file_name = malloc(PATH_MAX);
	buf = malloc(MAX_POLICY_STRLEN + 1);

	for (i = 0; ; i++) {
		int8_t policy;

		snprintf(file_name, PATH_MAX,
			 "/sys/class/scsi_host/host%d/link_power_management_policy",
			 i);

		fd = open(file_name, O_RDWR);
		if (fd < 0)
			break;

		len = read(fd, buf, MAX_POLICY_STRLEN);
		buf[len] = '\0';

		if (!strncmp(MAX_PERFORMANCE_STR, buf,
			     strlen(MAX_PERFORMANCE_STR)))
			policy = POLICY_MAX_PERFORMANCE;
		else if (!strncmp(MEDIUM_POWER_STR, buf,
				  strlen(MEDIUM_POWER_STR)))
			policy = POLICY_MEDIUM_POWER;
		else if (!strncmp(MIN_POWER_STR, buf,
				  strlen(MIN_POWER_STR)))
			policy = POLICY_MIN_POWER;
		else
			policy = POLICY_UNKNOWN;

		if (!(i % 256))
			link_pm_policies = realloc(link_pm_policies,
						   (i / 256 + 1) * 256 + 1);

		link_pm_policies[i] = policy;
		link_pm_policies[i + 1] = 0;

		/* If the policy is something we don't know about,
		 * don't touch it, since we might potentially break things.
		 * And we obviously don't need to touch anything if the
		 * setting is already correct...
		 */
		if (policy != POLICY_UNKNOWN &&
		    policy != POLICY_MIN_POWER) {
			lseek(fd, 0, SEEK_SET);
			igt_assert_eq(write(fd, MIN_POWER_STR,
					    strlen(MIN_POWER_STR)),
				      strlen(MIN_POWER_STR));
		}
		close(fd);
	}
	free(buf);
	free(file_name);

	return link_pm_policies;
}

/**
 * igt_pm_restore_sata_link_power_management:
 * @pm_data: An opaque pointer with saved link PM policies;
 *           If NULL is passed we force enable the "max_performance" policy.
 *
 * Restore the link power management policies to the values
 * prior to enabling min_power.
 *
 * Caveat: If the system supports hotplugging and hotplugging takes
 *         place during our testing so that the hosts change numbers
 *         we might restore the settings to the wrong hosts.
 */
void igt_pm_restore_sata_link_power_management(int8_t *pm_data)
{
	int fd, i;
	char *file_name;

	/* Disk runtime PM policies. */
	file_name = malloc(PATH_MAX);
	for (i = 0; ; i++) {
		int8_t policy;

		if (!pm_data)
			policy = POLICY_MAX_PERFORMANCE;
		else if (pm_data[i] == POLICY_UNKNOWN)
			continue;
		else
			policy = pm_data[i];

		snprintf(file_name, PATH_MAX,
			 "/sys/class/scsi_host/host%d/link_power_management_policy",
			 i);

		fd = open(file_name, O_WRONLY);
		if (fd < 0)
			break;

		switch (policy) {
		default:
		case POLICY_MAX_PERFORMANCE:
			igt_assert_eq(write(fd, MAX_PERFORMANCE_STR,
					    strlen(MAX_PERFORMANCE_STR)),
				      strlen(MAX_PERFORMANCE_STR));
			break;

		case POLICY_MEDIUM_POWER:
			igt_assert_eq(write(fd, MEDIUM_POWER_STR,
					    strlen(MEDIUM_POWER_STR)),
				      strlen(MEDIUM_POWER_STR));
			break;

		case POLICY_MIN_POWER:
			igt_assert_eq(write(fd, MIN_POWER_STR,
					    strlen(MIN_POWER_STR)),
				      strlen(MIN_POWER_STR));
			break;
		}

		close(fd);
	}
	free(file_name);
}
