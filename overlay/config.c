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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "overlay.h"

#define DEFAULT_SECTION "window"

static const char *skip_whitespace(const char *s, const char *end)
{
	while (s < end && isspace(*s))
		s++;
	return s;
}

static const char *trim_whitespace(const char *s, const char *end)
{
	if (end == NULL)
		return end;

	while (end > s && isspace(*--end))
		;

	return end + 1;
}

static int is_eol(int c)
{
	return c == '\n' || c == '\r';
}

static const char *skip_past_eol(const char *s, const char *end)
{
	while (s < end && !is_eol(*s++))
		;

	return s;
}

static const char *find(const char *s, const char *end, int c)
{
	while (s < end && *s != c) {
		if (*s == '#')
			break;

		if (*s == '\n')
			return NULL;
		s++;
	}

	return c == '\n' ? s : s < end ? s : NULL;
}

static int parse(const char *buf, int len,
		 int (*func)(const char *section,
			     const char *name,
			     const char *value,
			     void *data),
		 void *data)
{
	char section[128] = DEFAULT_SECTION, name[128], value[128];
	const char *buf_end = buf + len;
	const char *end;
	int has_section = 0;
	int line;

	for (line = 0 ; ++line; buf = skip_past_eol(buf, buf_end)) {
		buf = skip_whitespace(buf, buf_end);
		if (buf >= buf_end)
			break;

		if (*buf == ';' || *buf == '#') {
			/* comment */
		} else if (*buf == '[') { /* new section */
			end = find(++buf, buf_end, ']');
			if (end == NULL)
				return line;

			end = trim_whitespace(buf, end);
			if (end <= buf)
				continue;

			len = end - buf;
			if (len == 0 || len >= sizeof(section))
				return line;

			memcpy(section, buf, len);
			section[len] = '\0';
			has_section = 1;
		} else { /* name = value */
			const char *sep;
			int has_value = 1;

			sep = find(buf, buf_end, '=');
			if (sep == NULL)
				sep = find(buf, buf_end, ':');
			if (sep == NULL) {
				sep = find(buf, buf_end, '\n');
				has_value = 0;
			}
			end = trim_whitespace(buf, sep);
			if (end <= buf)
				continue;

			len = end - buf;
			if (len == 0 || len >= sizeof(name))
				return line;

			memcpy(name, buf, len);
			name[len] = '\0';

			if (has_value) {
				buf = skip_whitespace(sep + 1, buf_end);
				end = find(buf, buf_end, '\n');
				end = trim_whitespace(buf, end);

				len = end - buf;
				if (len >= sizeof(name))
					return line;

				memcpy(value, buf, len);
				value[len] = '\0';
			} else
				*value = '\0';

			if (!has_section) {
				char *dot;

				dot = strchr(name, '.');
				if (dot && dot[1]) {
					*dot = '\0';

					if (!func(name, dot+1, value, data))
						return line;

					continue;
				}
			}

			if (!func(section, name, value, data))
				return line;
		}
	}

	return 0;
}

static int add_value(const char *section,
		     const char *name,
		     const char *value,
		     void *data)
{
	struct config *c = data;
	struct config_section *s;
	struct config_value *v, **prev;

	for (s = c->sections; s != NULL; s = s->next)
		if (strcmp(s->name, section) == 0)
			break;
	if (s == NULL) {
		int len = strlen(section) + 1;

		s = malloc(sizeof(*s)+len);
		if (s == NULL)
			return 0;

		memcpy(s->name, section, len);
		s->values = NULL;
		s->next = c->sections;
		c->sections = s;
	}

	for (prev = &s->values; (v = *prev) != NULL; prev = &v->next) {
		if (strcmp(v->name, name) == 0) {
			*prev = v->next;
			free(v);
			break;
		}
	}
	{
		int name_len = strlen(name) + 1;
		int value_len = strlen(value) + 1;

		v = malloc(sizeof(*v) + name_len + value_len);
		if (v == NULL)
			return 0;

		v->name = memcpy(v+1, name, name_len);
		v->value = memcpy(v->name + name_len, value, value_len);

		v->next = s->values;
		s->values = v;
	}

	return 1;
}

static int config_init_from_file(struct config *config, const char *filename)
{
	struct stat st;
	int fd, err = -1;
	char *str;

	fd = open(filename, 0);
	if (fd < 0)
		return -1;

	if (fstat(fd, &st) < 0)
		goto err_fd;

	if ((str = mmap(0, st.st_size, PROT_READ, MAP_SHARED, fd, 0)) == (void *)-1)
		goto err_fd;

	err = parse(str, st.st_size, add_value, config);
	munmap(str, st.st_size);

err_fd:
	close(fd);
	return err;
}

void config_init(struct config *config)
{
	memset(config, 0, sizeof(*config));
}

void config_parse_string(struct config *config, const char *str)
{
	int err;

	if (str == NULL)
		return;

	err = config_init_from_file(config, str);
	if (err == -1)
		err = parse(str, strlen(str), add_value, config);
	if (err) {
		fprintf(stderr, "Failed to parse config string at line %d\n", err);
		exit(1);
	}
}

void config_set_value(struct config *c,
		      const char *section,
		      const char *name,
		      const char *value)
{
	add_value(section, name, value, c);
}

const char *config_get_value(struct config *c,
			     const char *section,
			     const char *name)
{
	struct config_section *s;
	struct config_value *v;

	for (s = c->sections; s != NULL; s = s->next) {
		if (strcmp(s->name, section))
			continue;

		for (v = s->values; v != NULL; v = v->next) {
			if (strcmp(v->name, name))
				continue;

			return v->value;
		}
	}

	return NULL;
}
