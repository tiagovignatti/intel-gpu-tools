/*
 * Copyright © 2015 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <ctype.h>
#include <errno.h>
#include <regex.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "intel_reg_spec.h"

static const struct port_desc port_descs[] = {
	{
		.name = "mmio",
		.port = PORT_MMIO,
		.stride = 4,
	},
	{
		.name = "portio-vga",
		.port = PORT_PORTIO_VGA,
		.stride = 4,
	},
	{
		.name = "mmio-vga",
		.port = PORT_MMIO_VGA,
		.stride = 4,
	},
	{
		.name = "bunit",
		.port = PORT_BUNIT,
		.stride = 1,
	},
	{
		.name = "punit",
		.port = PORT_PUNIT,
		.stride = 1,
	},
	{
		.name = "nc",
		.port = PORT_NC,
		.stride = 4,
	},
	{
		.name = "dpio",
		.port = PORT_DPIO,
		.stride = 4,
	},
	{
		.name = "gpio-nc",
		.port = PORT_GPIO_NC,
		.stride = 4,
	},
	{
		.name = "gpio_nc",
		.port = PORT_GPIO_NC,
		.stride = 4,
	},
	{
		.name = "cck",
		.port = PORT_CCK,
		.stride = 1,
	},
	{
		.name = "ccu",
		.port = PORT_CCU,
		.stride = 4,
	},
	{
		.name = "dpio2",
		.port = PORT_DPIO2,
		.stride = 4,
	},
	{
		.name = "flisdsi",
		.port = PORT_FLISDSI,
		.stride = 1,
	},
};

/*
 * Parse port desc of the form (PORTNAME|PORTNUM|MMIO-OFFSET) into reg. NULL or
 * zero length s is regarded as MMIO.
 */
int parse_port_desc(struct reg *reg, const char *s)
{
	enum port_addr port = PORT_NONE;
	int i;

	if (s && *s) {
		/* See if port is specified by number. */
		char *endp;
		unsigned long n = strtoul(s, &endp, 16);
		if (endp > s && *endp == 0) {
			if (n > PORT_MAX) {
				/* Not a sideband port, assume MMIO offset. */
				port = PORT_MMIO;
				reg->mmio_offset = n;
			} else {
				port = n;
				reg->mmio_offset = 0;
			}
		} else {
			reg->mmio_offset = 0;
		}
	} else {
		/* No port, default to searching for MMIO. */
		port = PORT_MMIO;
		reg->mmio_offset = 0;
	}

	for (i = 0; i < ARRAY_SIZE(port_descs); i++) {
		if ((port != PORT_NONE && port_descs[i].port == port) ||
		    (s && strcasecmp(s, port_descs[i].name) == 0)) {
			reg->port_desc = port_descs[i];
			return 0;
		}
	}

	return -1;
}

static const char *skip_space(const char *line)
{
	while (*line && isspace(*line))
		line++;

	return line;
}

static bool ignore_line(const char *line)
{
	line = skip_space(line);

	switch (*line) {
	case '\0':
	case '#':
	case ';':
		return true;
	case '/':
		return *(line + 1) == '/';
	}

	return false;
}

static char *include_file(const char *line, const char *source)
{
	char *filename, *p;

	line = skip_space(line);
	if (*line == '(')
		return NULL;

	/* this'll be plenty */
	filename = malloc(strlen(source) + strlen(line) + 1);
	if (!filename)
		return NULL;

	p = strrchr(source, '/');
	if (p && *line != '/') {
		int len = p - source + 1;

		memcpy(filename, source, len);
		strcpy(filename + len, line);
	} else {
		strcpy(filename, line);
	}

	p = strchr(filename, '\n');
	if (p)
		*p = '\0';

	return filename;
}

#define SPC	"[[:space:]]*"
#define SEP	SPC "," SPC
#define BEG	"^" SPC "\\(" SPC
#define END	SPC "\\)" SPC "$"
#define VALUE	"([[:print:]]*)"
#define QVALUE	"'" VALUE "'"
#define REGEXP	BEG QVALUE SEP QVALUE SEP QVALUE END

static int parse_line(struct reg *reg, const char *line)
{
	static regex_t regex;
	static bool initialized = false;
	regmatch_t match[4];
	int i, ret;

	if (!initialized) {
		if (regcomp (&regex, REGEXP, REG_EXTENDED)) {
			fprintf(stderr, "regcomp %s\n", REGEXP);
			return -1;
		}
		initialized = true;
	}

	memset(reg, 0, sizeof(*reg));

	ret = regexec(&regex, line, ARRAY_SIZE(match), match, 0);
	if (ret)
		ret = -1;

	for (i = 1; i < ARRAY_SIZE(match) && ret == 0; i++) {
		char *p, *e;

		p = strndup(line + match[i].rm_so,
			    match[i].rm_eo - match[i].rm_so);

		if (i == 1) {
			reg->name = p;
		} else if (i == 2) {
			reg->addr = strtoul(p, &e, 16);
			free(p);
			if (*e)
				ret = -1;
		} else if (i == 3) {
			ret = parse_port_desc(reg, p);
			free(p);
		}
	}

	if (ret)
		free(reg->name);

	return ret;
}

static ssize_t parse_file(struct reg **regs, size_t *nregs,
			  ssize_t index, const char *filename)
{
	FILE *file;
	char *line = NULL, *include;
	size_t linesize = 0;
	int lineno = 0, r;
	ssize_t ret = -1;

	file = fopen(filename, "r");
	if (!file) {
		fprintf(stderr, "Error: fopen '%s': %s\n",
			filename, strerror(errno));
		return -1;
	}

	while (getline(&line, &linesize, file) != -1) {
		struct reg reg;

		lineno++;

		if (ignore_line(line))
			continue;

		include = include_file(line, filename);
		if (include) {
			index = parse_file(regs, nregs, index, include);
			free(include);
			if (index < 0) {
				fprintf(stderr, "Error: %s:%d: %s",
					filename, lineno, line);
				goto out;
			}
			continue;
		}

		r = parse_line(&reg, line);
		if (r < 0) {
			fprintf(stderr, "Error: %s:%d: %s",
				filename, lineno, line);
			goto out;
		} else if (r) {
			continue;
		}

		if (!*regs || index >= *nregs) {
			if (!*regs)
				*nregs = 64;
			else
				*nregs *= 2;

			*regs = recalloc(*regs, *nregs, sizeof(**regs));
			if (!*regs) {
				fprintf(stderr, "Error: %s\n", strerror(ENOMEM));
				goto out;
			}
		}

		(*regs)[index++] = reg;
	}

	ret = index;

out:
	free(line);
	fclose(file);

	return ret;
}

/*
 * Get register definitions from file.
 */
ssize_t intel_reg_spec_file(struct reg **regs, const char *file)
{
	size_t nregs = 0;
	*regs = NULL;

	return parse_file(regs, &nregs, 0, file);
}

/*
 * Free the memory allocated for register definitions.
 */
void intel_reg_spec_free(struct reg *regs, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		free(regs[i].name);
	}
	free(regs);
}
