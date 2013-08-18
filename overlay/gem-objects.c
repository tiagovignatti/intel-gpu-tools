#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gem-objects.h"

/* /sys/kernel/debug/dri/0/i915_gem_objects:
 *	46 objects, 20107264 bytes
 *	42 [42] objects, 15863808 [15863808] bytes in gtt
 *	  0 [0] active objects, 0 [0] bytes
 *	  42 [42] inactive objects, 15863808 [15863808] bytes
 *	0 unbound objects, 0 bytes
 *	3 purgeable objects, 4456448 bytes
 *	30 pinned mappable objects, 3821568 bytes
 *	1 fault mappable objects, 3145728 bytes
 *	2145386496 [536870912] gtt total
 *
 *	Xorg: 35 objects, 16347136 bytes (0 active, 12103680 inactive, 0 unbound)
 */

int gem_objects_init(struct gem_objects *obj)
{
	char buf[8192], *b;
	int fd, len;

	memset(obj, 0, sizeof(*obj));

	fd = open("/sys/kernel/debug/dri/0/i915_gem_objects", 0);
	if (fd < 0)
		return errno;
	len = read(fd, buf, sizeof(buf)-1);
	close(fd);

	if (len < 0)
		return EIO;

	b = strstr(buf, "gtt total");
	if (b == NULL)
		return EIO;

	while (*b != '\n')
		b--;

	sscanf(b, "%ld [%ld]",
	       &obj->max_gtt, &obj->max_aperture);

	return 0;
}

static void insert_sorted(struct gem_objects *obj,
			  struct gem_objects_comm *comm)
{
	struct gem_objects_comm *next, **prev;

	for (prev = &obj->comm; (next = *prev) != NULL; prev = &next->next)
		if (comm->bytes > next->bytes)
			break;

	comm->next = *prev;
	*prev = comm;
}

int gem_objects_update(struct gem_objects *obj)
{
	char buf[8192], *b;
	struct gem_objects_comm *comm;
	struct gem_objects_comm *freed;
	int fd, len, ret;

	freed = obj->comm;
	obj->comm = NULL;

	fd = open("/sys/kernel/debug/dri/0/i915_gem_objects", 0);
	if (fd < 0) {
		ret = errno;
		goto done;
	}
	len = read(fd, buf, sizeof(buf)-1);
	close(fd);

	if (len < 0) {
		ret = EIO;
		goto done;
	}

	buf[len] = '\0';
	while (buf[--len] == '\n')
		buf[len] = '\0';

	b = buf;

	sscanf(b, "%d objects, %ld bytes",
	       &obj->total_count, &obj->total_bytes);

	b = strchr(b, '\n');
	sscanf(b, "%*d [%*d] objects, %ld [%ld] bytes in gtt",
	       &obj->total_gtt, &obj->total_aperture);

	ret = 0;
	b = strchr(b, ':');
	if (b == NULL)
		goto done;

	while (*b != '\n')
		b--;

	do {
		comm = freed;
		if (comm)
			freed = comm->next;
		else
			comm = malloc(sizeof(*comm));
		if (comm == NULL)
			break;

		/* Xorg: 35 objects, 16347136 bytes (0 active, 12103680 inactive, 0 unbound) */
		sscanf(++b, "%256s %u objects, %lu bytes",
		       comm->name, &comm->count, &comm->bytes);

		insert_sorted(obj, comm);
	} while ((b = strchr(b, '\n')) != NULL);

done:
	while (freed) {
		comm = freed;
		freed = comm->next;
		free(comm);
	}

	return ret;
}
