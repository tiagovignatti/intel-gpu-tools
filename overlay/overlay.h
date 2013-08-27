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

#ifndef OVERLAY_H
#define OVERLAY_H

#ifdef HAVE_CONFIG_H
#include"config.h"
#endif

#include <cairo.h>

enum position {
	POS_UNSET = -1,

	POS_LEFT = 0,
	POS_CENTRE = 1,
	POS_RIGHT = 2,

	POS_TOP = 0 << 4,
	POS_MIDDLE = 1 << 4,
	POS_BOTTOM = 2 << 4,

	POS_TOP_LEFT = POS_TOP | POS_LEFT,
	POS_TOP_CENTRE = POS_TOP | POS_CENTRE,
	POS_TOP_RIGHT = POS_TOP | POS_RIGHT,

	POS_MIDDLE_LEFT = POS_MIDDLE | POS_LEFT,
	POS_MIDDLE_CENTRE = POS_MIDDLE | POS_CENTRE,
	POS_MIDDLE_RIGHT = POS_MIDDLE | POS_RIGHT,

	POS_BOTTOM_LEFT = POS_BOTTOM | POS_LEFT,
	POS_BOTTOM_CENTRE = POS_BOTTOM | POS_CENTRE,
	POS_BOTTOM_RIGHT = POS_BOTTOM | POS_RIGHT,
};

struct overlay {
	cairo_surface_t *surface;
	void (*show)(struct overlay *);
	void (*hide)(struct overlay *);
};

extern const cairo_user_data_key_t overlay_key;

struct config {
	struct config_section {
		struct config_section *next;
		struct config_value {
			struct config_value *next;
			char *name;
			char *value;
		} *values;
		char name[0];
	} *sections;
};

void config_init(struct config *config);
void config_parse_string(struct config *config, const char *str);
void config_set_value(struct config *config,
		      const char *section,
		      const char *name,
		      const char *value);
const char *config_get_value(struct config *config,
			     const char *section,
			     const char *name);

#ifdef HAVE_OVERLAY_XVLIB
cairo_surface_t *x11_overlay_create(struct config *config, int *width, int *height);
void x11_overlay_stop(void);
#else
static inline cairo_surface_t *x11_overlay_create(struct config *config, int *width, int *height) { return NULL; }
static inline void x11_overlay_stop(void) { }
#endif

#ifdef HAVE_OVERLAY_XLIB
cairo_surface_t *x11_window_create(struct config *config, int *width, int *height);
#else
static inline cairo_surface_t *x11_window_create(struct config *config, int *width, int *height) { return NULL; }
#endif

cairo_surface_t *kms_overlay_create(struct config *config, int *width, int *height);

#endif /* OVERLAY_H */
