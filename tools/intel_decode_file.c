#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "intel_decode.h"

static void
read_data_text (const char *filename, uint32_t devid, int is_batch)
{
    FILE *file;
    uint32_t *data = NULL;
    int data_size = 0, count = 0, line_number = 0, matched;
    char *line = NULL;
    size_t line_size;
    uint32_t offset, value;
    uint32_t gtt_offset = 0, new_gtt_offset;
    char *buffer_type = is_batch ? "batchbuffer" : "ringbuffer";

    file = fopen (filename, "r");
    if (file == NULL) {
	fprintf (stderr, "Failed to open %s: %s\n",
		 filename, strerror (errno));
	exit (1);
    }

    while (getline (&line, &line_size, file) > 0) {
	line_number++;

	matched = sscanf (line, "--- gtt_offset = 0x%08x\n", &new_gtt_offset);
	if (matched == 1) {
	    if (count) {
		printf("%s at 0x%08x:\n", buffer_type, gtt_offset);
		intel_decode (data, count, gtt_offset, devid, 0);
		count = 0;
	    }
	    gtt_offset = new_gtt_offset;
	    continue;
	}

	matched = sscanf (line, "%08x : %08x", &offset, &value);
	if (matched !=2 ) {
	    fprintf (stderr, "Warning: Ignoring unrecognized line at %s:%d:\n%s",
		     filename, line_number, line);
	    continue;
	}

	count++;

	if (count > data_size) {
	    data_size = data_size ? data_size * 2 : 1024;
	    data = realloc (data, data_size * sizeof (uint32_t));
	    if (data == NULL) {
		fprintf (stderr, "Out of memory.\n");
		exit (1);
	    }
	}

	data[count-1] = value;
    }

    if (count) {
	printf("%s at 0x%08x:\n", buffer_type, gtt_offset);
	intel_decode (data, count, gtt_offset, devid, 0);
    }

    free (data);
    free (line);

    fclose (file);
}

static int
read_data_file (const char *filename, uint32_t devid, int is_batch)
{
    FILE *file;
    uint32_t *buf;
    uint32_t len = 4096;
    int count = 0;

    buf = malloc (sizeof (uint32_t) * len);
    if (buf == NULL) {
	fprintf (stderr, "Failed to allocate memory for %s.\n",
		 filename);
	return 0;
    }

    file = fopen (filename, "rb");
    if (file == NULL) {
	free (buf);
	fprintf (stderr, "Failed to open %s: %s\n",
		 filename, strerror (errno));
	return 0;
    }

    do {
	uint32_t *newbuf;

	count += fread (buf + count, sizeof (uint32_t), len - count, file);
	if (count < len)
	    break;

	len *= 2;
	newbuf = realloc (buf, len * sizeof (uint32_t));
	if (newbuf == NULL) {
	    free (buf);
	    fclose (file);
	    fprintf (stderr, "Failed to allocate memory for %s.\n",
		     filename);
	    return 0;
	}

	buf = newbuf;
    } while (1);

    fclose (file);

    intel_decode (buf, count, 0x0, devid, 1);
    intel_decode_context_reset ();
    free (buf);

    return 1;
}

int
main (int argc, char **argv)
{
	int is_text = 0;
	int i;
	uint32_t devid =0x27A2;

	for (i = 1; i < argc; i++) {
		if (strncmp (argv[i], "--pci-id=", 9) == 0) {
			devid = atoi (argv[i] + 9);
			continue;
		}

		if (is_text)
			read_data_text (argv[i], devid, 1);
		else
			read_data_file (argv[i], devid, 1);
	}

	return 0;
}
