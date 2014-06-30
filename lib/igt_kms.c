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
 * Authors:
 * 	Daniel Vetter <daniel.vetter@ffwll.ch>
 * 	Damien Lespiau <damien.lespiau@intel.com>
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <linux/kd.h>
#include <errno.h>

#include <i915_drm.h>

#include "drmtest.h"
#include "igt_kms.h"
#include "igt_aux.h"

/**
 * SECTION:igt_kms
 * @short_description: Kernel modesetting support library
 * @title: i-g-t kms
 * @include: igt_kms.h
 *
 * This library provides support to enumerate and set modeset configurations.
 *
 * Since this library is very much still a work-in-progress and the interfaces
 * still in-flux detailed api documentation is currently still missing.
 *
 * Note that this library's header pulls in the [i-g-t framebuffer](intel-gpu-tools-i-g-t-framebuffer.html)
 * library as a dependency.
 */
const char *kmstest_pipe_str(int pipe)
{
	const char *str[] = { "A", "B", "C" };

	if (pipe > 2)
		return "invalid";

	return str[pipe];
}

struct type_name {
	int type;
	const char *name;
};

#define type_name_fn(res) \
const char * kmstest_##res##_str(int type) {		\
	unsigned int i;					\
	for (i = 0; i < ARRAY_SIZE(res##_names); i++) { \
		if (res##_names[i].type == type)	\
			return res##_names[i].name;	\
	}						\
	return "(invalid)";				\
}

struct type_name encoder_type_names[] = {
	{ DRM_MODE_ENCODER_NONE, "none" },
	{ DRM_MODE_ENCODER_DAC, "DAC" },
	{ DRM_MODE_ENCODER_TMDS, "TMDS" },
	{ DRM_MODE_ENCODER_LVDS, "LVDS" },
	{ DRM_MODE_ENCODER_TVDAC, "TVDAC" },
};

type_name_fn(encoder_type)

struct type_name connector_status_names[] = {
	{ DRM_MODE_CONNECTED, "connected" },
	{ DRM_MODE_DISCONNECTED, "disconnected" },
	{ DRM_MODE_UNKNOWNCONNECTION, "unknown" },
};

type_name_fn(connector_status)

struct type_name connector_type_names[] = {
	{ DRM_MODE_CONNECTOR_Unknown, "unknown" },
	{ DRM_MODE_CONNECTOR_VGA, "VGA" },
	{ DRM_MODE_CONNECTOR_DVII, "DVI-I" },
	{ DRM_MODE_CONNECTOR_DVID, "DVI-D" },
	{ DRM_MODE_CONNECTOR_DVIA, "DVI-A" },
	{ DRM_MODE_CONNECTOR_Composite, "composite" },
	{ DRM_MODE_CONNECTOR_SVIDEO, "s-video" },
	{ DRM_MODE_CONNECTOR_LVDS, "LVDS" },
	{ DRM_MODE_CONNECTOR_Component, "component" },
	{ DRM_MODE_CONNECTOR_9PinDIN, "9-pin DIN" },
	{ DRM_MODE_CONNECTOR_DisplayPort, "DP" },
	{ DRM_MODE_CONNECTOR_HDMIA, "HDMI-A" },
	{ DRM_MODE_CONNECTOR_HDMIB, "HDMI-B" },
	{ DRM_MODE_CONNECTOR_TV, "TV" },
	{ DRM_MODE_CONNECTOR_eDP, "eDP" },
};

type_name_fn(connector_type)

static const char *mode_stereo_name(const drmModeModeInfo *mode)
{
	switch (mode->flags & DRM_MODE_FLAG_3D_MASK) {
	case DRM_MODE_FLAG_3D_FRAME_PACKING:
		return "FP";
	case DRM_MODE_FLAG_3D_FIELD_ALTERNATIVE:
		return "FA";
	case DRM_MODE_FLAG_3D_LINE_ALTERNATIVE:
		return "LA";
	case DRM_MODE_FLAG_3D_SIDE_BY_SIDE_FULL:
		return "SBSF";
	case DRM_MODE_FLAG_3D_L_DEPTH:
		return "LD";
	case DRM_MODE_FLAG_3D_L_DEPTH_GFX_GFX_DEPTH:
		return "LDGFX";
	case DRM_MODE_FLAG_3D_TOP_AND_BOTTOM:
		return "TB";
	case DRM_MODE_FLAG_3D_SIDE_BY_SIDE_HALF:
		return "SBSH";
	default:
		return NULL;
	}
}

void kmstest_dump_mode(drmModeModeInfo *mode)
{
	const char *stereo = mode_stereo_name(mode);

	printf("  %s %d %d %d %d %d %d %d %d %d 0x%x 0x%x %d%s%s%s\n",
	       mode->name,
	       mode->vrefresh,
	       mode->hdisplay,
	       mode->hsync_start,
	       mode->hsync_end,
	       mode->htotal,
	       mode->vdisplay,
	       mode->vsync_start,
	       mode->vsync_end,
	       mode->vtotal,
	       mode->flags,
	       mode->type,
	       mode->clock,
	       stereo ? " (3D:" : "",
	       stereo ? stereo : "",
	       stereo ? ")" : "");
	fflush(stdout);
}

int kmstest_get_pipe_from_crtc_id(int fd, int crtc_id)
{
	struct drm_i915_get_pipe_from_crtc_id pfci;
	int ret;

	memset(&pfci, 0, sizeof(pfci));
	pfci.crtc_id = crtc_id;
	ret = drmIoctl(fd, DRM_IOCTL_I915_GET_PIPE_FROM_CRTC_ID, &pfci);
	igt_assert(ret == 0);

	return pfci.pipe;
}

void kmstest_set_connector_dpms(int fd, drmModeConnector *connector, int mode)
{
	int i, dpms = 0;
	bool found_it = false;

	for (i = 0; i < connector->count_props; i++) {
		struct drm_mode_get_property prop;

		prop.prop_id = connector->props[i];
		prop.count_values = 0;
		prop.count_enum_blobs = 0;
		if (drmIoctl(fd, DRM_IOCTL_MODE_GETPROPERTY, &prop))
			continue;

		if (strcmp(prop.name, "DPMS"))
			continue;

		dpms = prop.prop_id;
		found_it = true;
		break;
	}
	igt_assert_f(found_it, "DPMS property not found on %d\n",
		     connector->connector_id);

	igt_assert(drmModeConnectorSetProperty(fd, connector->connector_id,
					       dpms, mode) == 0);
}

static signed long set_vt_mode(unsigned long mode)
{
	int fd;
	unsigned long prev_mode;

	fd = open("/dev/tty0", O_RDONLY);
	if (fd < 0)
		return -errno;

	prev_mode = 0;
	if (drmIoctl(fd, KDGETMODE, &prev_mode))
		goto err;
	if (drmIoctl(fd, KDSETMODE, (void *)mode))
		goto err;

	close(fd);

	return prev_mode;
err:
	close(fd);

	return -errno;
}

static unsigned long orig_vt_mode = -1UL;

static void restore_vt_mode_at_exit(int sig)
{
	if (orig_vt_mode != -1UL)
		set_vt_mode(orig_vt_mode);
}

/*
 * Set the VT to graphics mode and install an exit handler to restore the
 * original mode.
 */

void igt_set_vt_graphics_mode(void)
{
	long ret;

	igt_install_exit_handler(restore_vt_mode_at_exit);

	igt_disable_exit_handler();
	ret = set_vt_mode(KD_GRAPHICS);
	igt_enable_exit_handler();

	igt_assert(ret >= 0);
	orig_vt_mode = ret;
}

int kmstest_get_connector_default_mode(int drm_fd, drmModeConnector *connector,
				      drmModeModeInfo *mode)
{
	drmModeRes *resources;
	int i;

	resources = drmModeGetResources(drm_fd);
	if (!resources) {
		perror("drmModeGetResources failed");

		return -1;
	}

	if (!connector->count_modes) {
		fprintf(stderr, "no modes for connector %d\n",
			connector->connector_id);
		drmModeFreeResources(resources);

		return -1;
	}

	for (i = 0; i < connector->count_modes; i++) {
		if (i == 0 ||
		    connector->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
			*mode = connector->modes[i];
			if (mode->type & DRM_MODE_TYPE_PREFERRED)
				break;
		}
	}

	drmModeFreeResources(resources);

	return 0;
}

int kmstest_get_connector_config(int drm_fd, uint32_t connector_id,
				 unsigned long crtc_idx_mask,
				 struct kmstest_connector_config *config)
{
	drmModeRes *resources;
	drmModeConnector *connector;
	drmModeEncoder *encoder;
	int i, j;

	resources = drmModeGetResources(drm_fd);
	if (!resources) {
		perror("drmModeGetResources failed");
		goto err1;
	}

	/* First, find the connector & mode */
	connector = drmModeGetConnector(drm_fd, connector_id);
	if (!connector)
		goto err2;

	if (connector->connection != DRM_MODE_CONNECTED)
		goto err3;

	if (!connector->count_modes) {
		fprintf(stderr, "connector %d has no modes\n", connector_id);
		goto err3;
	}

	if (connector->connector_id != connector_id) {
		fprintf(stderr, "connector id doesn't match (%d != %d)\n",
			connector->connector_id, connector_id);
		goto err3;
	}

	/*
	 * Find given CRTC if crtc_id != 0 or else the first CRTC not in use.
	 * In both cases find the first compatible encoder and skip the CRTC
	 * if there is non such.
	 */
	encoder = NULL;		/* suppress GCC warning */
	for (i = 0; i < resources->count_crtcs; i++) {
		if (!resources->crtcs[i] || !(crtc_idx_mask & (1 << i)))
			continue;

		/* Now get a compatible encoder */
		for (j = 0; j < connector->count_encoders; j++) {
			encoder = drmModeGetEncoder(drm_fd,
						    connector->encoders[j]);

			if (!encoder) {
				fprintf(stderr, "could not get encoder %d: %s\n",
					resources->encoders[j], strerror(errno));

				continue;
			}

			if (encoder->possible_crtcs & (1 << i))
				goto found;

			drmModeFreeEncoder(encoder);
		}
	}

	goto err3;

found:
	if (kmstest_get_connector_default_mode(drm_fd, connector,
				       &config->default_mode) < 0)
		goto err4;

	config->connector = connector;
	config->encoder = encoder;
	config->crtc = drmModeGetCrtc(drm_fd, resources->crtcs[i]);
	config->crtc_idx = i;
	config->pipe = kmstest_get_pipe_from_crtc_id(drm_fd,
						     config->crtc->crtc_id);

	drmModeFreeResources(resources);

	return 0;
err4:
	drmModeFreeEncoder(encoder);
err3:
	drmModeFreeConnector(connector);
err2:
	drmModeFreeResources(resources);
err1:
	return -1;
}

void kmstest_free_connector_config(struct kmstest_connector_config *config)
{
	drmModeFreeCrtc(config->crtc);
	drmModeFreeEncoder(config->encoder);
	drmModeFreeConnector(config->connector);
}

const char *plane_name(enum igt_plane p)
{
	static const char *names[] = {
		[IGT_PLANE_1] = "plane1",
		[IGT_PLANE_2] = "plane2",
		[IGT_PLANE_3] = "plane3",
		[IGT_PLANE_CURSOR] = "cursor",
	};

	igt_assert(p < ARRAY_SIZE(names) && names[p]);

	return names[p];
}

/*
 * A small modeset API
 */

#define LOG_SPACES		"    "
#define LOG_N_SPACES		(sizeof(LOG_SPACES) - 1)

#define LOG_INDENT(d, section)				\
	do {						\
		igt_display_log(d, "%s {\n", section);	\
		igt_display_log_shift(d, 1);		\
	} while (0)
#define LOG_UNINDENT(d)					\
	do {						\
		igt_display_log_shift(d, -1);		\
		igt_display_log(d, "}\n");		\
	} while (0)
#define LOG(d, fmt, ...)	igt_display_log(d, fmt, ## __VA_ARGS__)

static void  __attribute__((format(printf, 2, 3)))
igt_display_log(igt_display_t *display, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	igt_debug("display: ");
	for (i = 0; i < display->log_shift; i++)
		igt_debug("%s", LOG_SPACES);
	igt_vlog(IGT_LOG_DEBUG, fmt, args);
	va_end(args);
}

static void igt_display_log_shift(igt_display_t *display, int shift)
{
	display->log_shift += shift;
	igt_assert(display->log_shift >= 0);
}

static void igt_output_refresh(igt_output_t *output)
{
	igt_display_t *display = output->display;
	int ret;
	unsigned long crtc_idx_mask;

	/* we mask out the pipes already in use */
	crtc_idx_mask = output->pending_crtc_idx_mask & ~display->pipes_in_use;

	if (output->valid)
		kmstest_free_connector_config(&output->config);
	ret = kmstest_get_connector_config(display->drm_fd,
					   output->id,
					   crtc_idx_mask,
					   &output->config);
	if (ret == 0)
		output->valid = true;
	else
		output->valid = false;

	if (!output->valid)
		return;

	if (!output->name) {
		drmModeConnector *c = output->config.connector;

		asprintf(&output->name, "%s-%d",
			 kmstest_connector_type_str(c->connector_type),
			 c->connector_type_id);
	}

	LOG(display, "%s: Selecting pipe %c\n", output->name,
	    pipe_name(output->config.pipe));

	display->pipes_in_use |= 1 << output->config.pipe;
}

/*
 * Walk a plane's property list to determine its type.  If we don't
 * find a type property, then the kernel doesn't support universal
 * planes and we know the plane is an overlay/sprite.
 */
static int get_drm_plane_type(igt_display_t *display, uint32_t plane_id)
{
	drmModeObjectPropertiesPtr proplist;
	drmModePropertyPtr prop = NULL;
	int type = DRM_PLANE_TYPE_OVERLAY;
	int i;

	proplist = drmModeObjectGetProperties(display->drm_fd,
					      plane_id,
					      DRM_MODE_OBJECT_PLANE);
	for (i = 0; i < proplist->count_props; i++) {
		drmModeFreeProperty(prop);
		prop = drmModeGetProperty(display->drm_fd, proplist->props[i]);
		if (!prop)
			continue;

		if (strcmp(prop->name, "type") == 0) {
			type = proplist->prop_values[i];
			break;
		}
	}

	drmModeFreeProperty(prop);
	drmModeFreeObjectProperties(proplist);

	return type;
}

void igt_display_init(igt_display_t *display, int drm_fd)
{
	drmModeRes *resources;
	drmModePlaneRes *plane_resources;
	int i;

	memset(display, 0, sizeof(igt_display_t));

	LOG_INDENT(display, "init");

	display->drm_fd = drm_fd;

	resources = drmModeGetResources(display->drm_fd);
	igt_assert(resources);

	/*
	 * We cache the number of pipes, that number is a physical limit of the
	 * hardware and cannot change of time (for now, at least).
	 */
	display->n_pipes = resources->count_crtcs;

	drmSetClientCap(drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	plane_resources = drmModeGetPlaneResources(display->drm_fd);
	igt_assert(plane_resources);

	for (i = 0; i < display->n_pipes; i++) {
		igt_pipe_t *pipe = &display->pipes[i];
		igt_plane_t *plane;
		int p = IGT_PLANE_2;
		int j, type;

		pipe->display = display;
		pipe->pipe = i;

		/* add the planes that can be used with that pipe */
		for (j = 0; j < plane_resources->count_planes; j++) {
			drmModePlane *drm_plane;

			drm_plane = drmModeGetPlane(display->drm_fd,
						    plane_resources->planes[j]);
			igt_assert(drm_plane);

			if (!(drm_plane->possible_crtcs & (1 << i))) {
				drmModeFreePlane(drm_plane);
				continue;
			}

			type = get_drm_plane_type(display,
						  plane_resources->planes[j]);
			switch (type) {
			case DRM_PLANE_TYPE_PRIMARY:
				plane = &pipe->planes[IGT_PLANE_PRIMARY];
				plane->is_primary = 1;
				plane->index = IGT_PLANE_PRIMARY;
				display->has_universal_planes = 1;
				break;
			case DRM_PLANE_TYPE_CURSOR:
				/*
				 * Cursor should be the highest index in our
				 * internal list, but we don't know what that
				 * is yet.  Just stick it in the last slot
				 * for now and we'll move it later, if
				 * necessary.
				 */
				plane = &pipe->planes[IGT_PLANE_CURSOR];
				plane->is_cursor = 1;
				plane->index = IGT_PLANE_CURSOR;
				display->has_universal_planes = 1;
				break;
			default:
				plane = &pipe->planes[p];
				plane->index = p++;
				break;
			}

			plane->pipe = pipe;
			plane->drm_plane = drm_plane;
		}

		if (display->has_universal_planes) {
			/*
			 * If we have universal planes, we should have both
			 * primary and cursor planes setup now.
			 */
			igt_assert(pipe->planes[IGT_PLANE_PRIMARY].drm_plane &&
				   pipe->planes[IGT_PLANE_CURSOR].drm_plane);

			/*
			 * Cursor was put in the last slot.  If we have 0 or
			 * only 1 sprite, that's the wrong slot and we need to
			 * move it down.
			 */
			if (p != IGT_PLANE_CURSOR) {
				pipe->planes[p] = pipe->planes[IGT_PLANE_CURSOR];
				pipe->planes[p].index = p;
				memset(&pipe->planes[IGT_PLANE_CURSOR], 0,
				       sizeof *plane);
			}
		} else {
			/*
			 * No universal plane support.  Add drm_plane-less
			 * primary and cursor planes.
			 */
			plane = &pipe->planes[IGT_PLANE_PRIMARY];
			plane->pipe = pipe;
			plane->index = IGT_PLANE_PRIMARY;
			plane->is_primary = true;

			plane = &pipe->planes[p];
			plane->pipe = pipe;
			plane->index = p;
			plane->is_cursor = true;
		}

		/* planes = 1 primary, (p-1) sprites, 1 cursor */
		pipe->n_planes = p + 1;

		/* make sure we don't overflow the plane array */
		igt_assert(pipe->n_planes <= IGT_MAX_PLANES);
	}

	/*
	 * The number of connectors is set, so we just initialize the outputs
	 * array in _init(). This may change when we need dynamic connectors
	 * (say DisplayPort MST).
	 */
	display->n_outputs = resources->count_connectors;
	display->outputs = calloc(display->n_outputs, sizeof(igt_output_t));
	igt_assert(display->outputs);

	for (i = 0; i < display->n_outputs; i++) {
		igt_output_t *output = &display->outputs[i];

		/*
		 * We're free to select any pipe to drive that output until
		 * a constraint is set with igt_output_set_pipe().
		 */
		output->pending_crtc_idx_mask = -1UL;
		output->id = resources->connectors[i];
		output->display = display;

		igt_output_refresh(output);
	}

	drmModeFreePlaneResources(plane_resources);
	drmModeFreeResources(resources);

	LOG_UNINDENT(display);
}

int igt_display_get_n_pipes(igt_display_t *display)
{
	return display->n_pipes;
}

static void igt_pipe_fini(igt_pipe_t *pipe)
{
	int i;

	for (i = 0; i < pipe->n_planes; i++) {
		igt_plane_t *plane = &pipe->planes[i];

		if (plane->drm_plane) {
			drmModeFreePlane(plane->drm_plane);
			plane->drm_plane = NULL;
		}
	}
}

static void igt_output_fini(igt_output_t *output)
{
	if (output->valid)
		kmstest_free_connector_config(&output->config);
	free(output->name);
}

void igt_display_fini(igt_display_t *display)
{
	int i;

	for (i = 0; i < display->n_pipes; i++)
		igt_pipe_fini(&display->pipes[i]);

	for (i = 0; i < display->n_outputs; i++)
		igt_output_fini(&display->outputs[i]);
	free(display->outputs);
	display->outputs = NULL;
}

static void igt_display_refresh(igt_display_t *display)
{
	int i, j;

	display->pipes_in_use = 0;

       /* Check that two outputs aren't trying to use the same pipe */
        for (i = 0; i < display->n_outputs; i++) {
                igt_output_t *a = &display->outputs[i];

                if (a->pending_crtc_idx_mask == -1UL)
                        continue;

                for (j = 0; j < display->n_outputs; j++) {
                        igt_output_t *b = &display->outputs[j];

                        if (i == j)
                                continue;

                        if (b->pending_crtc_idx_mask == -1UL)
                                continue;

                        igt_assert_f(a->pending_crtc_idx_mask !=
                                     b->pending_crtc_idx_mask,
                                     "%s and %s are both trying to use pipe %c\n",
                                     igt_output_name(a), igt_output_name(b),
                                     pipe_name(ffs(a->pending_crtc_idx_mask) - 1));
                }
        }

	/*
	 * The pipe allocation has to be done in two phases:
	 *   - first, try to satisfy the outputs where a pipe has been specified
	 *   - then, allocate the outputs with PIPE_ANY
	 */
	for (i = 0; i < display->n_outputs; i++) {
		igt_output_t *output = &display->outputs[i];

		if (output->pending_crtc_idx_mask == -1UL)
			continue;

		igt_output_refresh(output);
	}
	for (i = 0; i < display->n_outputs; i++) {
		igt_output_t *output = &display->outputs[i];

		if (output->pending_crtc_idx_mask != -1UL)
			continue;

		igt_output_refresh(output);
	}
}

static igt_pipe_t *igt_output_get_driving_pipe(igt_output_t *output)
{
	igt_display_t *display = output->display;
	enum pipe pipe;

	if (output->pending_crtc_idx_mask == -1UL) {
		/*
		 * The user hasn't specified a pipe to use, take the one
		 * configured by the last refresh()
		 */
		pipe = output->config.pipe;
	} else {
		/*
		 * Otherwise, return the pending pipe (ie the pipe that should
		 * drive this output after the commit()
		 */
		pipe = ffs(output->pending_crtc_idx_mask) - 1;
	}

	igt_assert(pipe >= 0 && pipe < display->n_pipes);

	return &display->pipes[pipe];
}

static igt_plane_t *igt_pipe_get_plane(igt_pipe_t *pipe, enum igt_plane plane)
{
	int idx;

	/* Cursor plane is always the highest index */
	if (plane == IGT_PLANE_CURSOR)
		idx = pipe->n_planes - 1;
	else {
		igt_assert_f(plane >= 0 && plane < (pipe->n_planes),
			     "plane=%d\n", plane);
		idx = plane;
	}

	return &pipe->planes[idx];
}

static uint32_t igt_plane_get_fb_id(igt_plane_t *plane)
{
	if (plane->fb)
		return plane->fb->fb_id;
	else
		return 0;
}

static uint32_t igt_plane_get_fb_gem_handle(igt_plane_t *plane)
{
	if (plane->fb)
		return plane->fb->gem_handle;
	else
		return 0;
}

#define CHECK_RETURN(r, fail) {	\
	if (r && !fail)		\
		return r;	\
	igt_assert(r == 0);	\
}

/*
 * Commit position and fb changes to a DRM plane via the SetPlane ioctl; if the
 * DRM call to program the plane fails, we'll either fail immediately (for
 * tests that expect the commit to succeed) or return the failure code (for
 * tests that expect a specific error code).
 */
static int igt_drm_plane_commit(igt_plane_t *plane,
				igt_output_t *output,
				bool fail_on_error)
{
	igt_display_t *display = output->display;
	uint32_t fb_id, crtc_id;
	int ret;

	igt_assert(plane->drm_plane);

	fb_id = igt_plane_get_fb_id(plane);
	crtc_id = output->config.crtc->crtc_id;

	if (plane->fb_changed && fb_id == 0) {
		LOG(display,
		    "%s: SetPlane pipe %c, plane %d, disabling\n",
		    igt_output_name(output),
		    pipe_name(output->config.pipe),
		    plane->index);

		ret = drmModeSetPlane(display->drm_fd,
				      plane->drm_plane->plane_id,
				      crtc_id,
				      fb_id,
				      0,    /* flags */
				      0, 0, /* crtc_x, crtc_y */
				      0, 0, /* crtc_w, crtc_h */
				      IGT_FIXED(0,0), /* src_x */
				      IGT_FIXED(0,0), /* src_y */
				      IGT_FIXED(0,0), /* src_w */
				      IGT_FIXED(0,0) /* src_h */);

		CHECK_RETURN(ret, fail_on_error);
	} else if (plane->fb_changed || plane->position_changed) {
		LOG(display,
		    "%s: SetPlane %c.%d, fb %u, position (%d, %d)\n",
		    igt_output_name(output),
		    pipe_name(output->config.pipe),
		    plane->index,
		    fb_id,
		    plane->crtc_x, plane->crtc_y);

		ret = drmModeSetPlane(display->drm_fd,
				      plane->drm_plane->plane_id,
				      crtc_id,
				      fb_id,
				      0,    /* flags */
				      plane->crtc_x, plane->crtc_y,
				      plane->fb->width, plane->fb->height,
				      IGT_FIXED(0,0), /* src_x */
				      IGT_FIXED(0,0), /* src_y */
				      IGT_FIXED(plane->fb->width,0), /* src_w */
				      IGT_FIXED(plane->fb->height,0) /* src_h */);

		CHECK_RETURN(ret, fail_on_error);
	}

	plane->fb_changed = false;
	plane->position_changed = false;
	return 0;
}

/*
 * Commit position and fb changes to a cursor via legacy ioctl's.  If commit
 * fails, we'll either fail immediately (for tests that expect the commit to
 * succeed) or return the failure code (for tests that expect a specific error
 * code).
 */
static int igt_cursor_commit_legacy(igt_plane_t *cursor,
				    igt_output_t *output,
				    bool fail_on_error)
{
	igt_display_t *display = output->display;
	uint32_t crtc_id = output->config.crtc->crtc_id;
	int ret;

	if (cursor->fb_changed) {
		uint32_t gem_handle = igt_plane_get_fb_gem_handle(cursor);

		if (gem_handle) {
			LOG(display,
			    "%s: SetCursor pipe %c, fb %u %dx%d\n",
			    igt_output_name(output),
			    pipe_name(output->config.pipe),
			    gem_handle,
			    cursor->fb->width, cursor->fb->height);

			ret = drmModeSetCursor(display->drm_fd, crtc_id,
					       gem_handle,
					       cursor->fb->width,
					       cursor->fb->height);
		} else {
			LOG(display,
			    "%s: SetCursor pipe %c, disabling\n",
			    igt_output_name(output),
			    pipe_name(output->config.pipe));

			ret = drmModeSetCursor(display->drm_fd, crtc_id,
					       0, 0, 0);
		}

		CHECK_RETURN(ret, fail_on_error);

		cursor->fb_changed = false;
	}

	if (cursor->position_changed) {
		int x = cursor->crtc_x;
		int y = cursor->crtc_y;

		LOG(display,
		    "%s: MoveCursor pipe %c, (%d, %d)\n",
		    igt_output_name(output),
		    pipe_name(output->config.pipe),
		    x, y);

		ret = drmModeMoveCursor(display->drm_fd, crtc_id, x, y);
		CHECK_RETURN(ret, fail_on_error);

		cursor->position_changed = false;
	}

	return 0;
}

/*
 * Commit position and fb changes to a primary plane via the legacy interface
 * (setmode).
 */
static int igt_primary_plane_commit_legacy(igt_plane_t *primary,
					   igt_output_t *output,
					   bool fail_on_error)
{
	struct igt_display *display = primary->pipe->display;
	drmModeModeInfo *mode;
	uint32_t fb_id, crtc_id;
	int ret;

	/* Primary planes can't be windowed when using a legacy commit */
	igt_assert((primary->crtc_x == 0 && primary->crtc_y == 0));

	if (!primary->fb_changed && !primary->position_changed)
		return 0;

	crtc_id = output->config.crtc->crtc_id;
	fb_id = igt_plane_get_fb_id(primary);
	if (fb_id)
		mode = igt_output_get_mode(output);
	else
		mode = NULL;

	if (fb_id) {
		LOG(display,
		    "%s: SetCrtc pipe %c, fb %u, panning (%d, %d), "
		    "mode %dx%d\n",
		    igt_output_name(output),
		    pipe_name(output->config.pipe),
		    fb_id,
		    0, 0,
		    mode->hdisplay, mode->vdisplay);

		ret = drmModeSetCrtc(display->drm_fd,
				     crtc_id,
				     fb_id,
				     0, 0, /* x, y */
				     &output->id,
				     1,
				     mode);
	} else {
		LOG(display,
		    "%s: SetCrtc pipe %c, disabling\n",
		    igt_output_name(output),
		    pipe_name(output->config.pipe));

		ret = drmModeSetCrtc(display->drm_fd,
				     crtc_id,
				     fb_id,
				     0, 0, /* x, y */
				     NULL, /* connectors */
				     0,    /* n_connectors */
				     NULL  /* mode */);
	}

	CHECK_RETURN(ret, fail_on_error);

	primary->pipe->enabled = (fb_id != 0);
	primary->fb_changed = false;

	return 0;
}


/*
 * Commit position and fb changes to a plane.  The value of @s will determine
 * which API is used to do the programming.
 */
static int igt_plane_commit(igt_plane_t *plane,
			    igt_output_t *output,
			    enum igt_commit_style s,
			    bool fail_on_error)
{
	if (plane->is_cursor && s == COMMIT_LEGACY) {
		return igt_cursor_commit_legacy(plane, output, fail_on_error);
	} else if (plane->is_primary && s == COMMIT_LEGACY) {
		return igt_primary_plane_commit_legacy(plane, output,
						       fail_on_error);
	} else {
		return igt_drm_plane_commit(plane, output, fail_on_error);
	}
}

/*
 * Commit all plane changes to an output.  Note that if @s is COMMIT_LEGACY,
 * enabling/disabling the primary plane will also enable/disable the CRTC.
 *
 * If @fail_on_error is true, any failure to commit plane state will lead
 * to subtest failure in the specific function where the failure occurs.
 * Otherwise, the first error code encountered will be returned and no
 * further programming will take place, which may result in some changes
 * taking effect and others not taking effect.
 */
static int igt_output_commit(igt_output_t *output,
			     enum igt_commit_style s,
			     bool fail_on_error)
{
	igt_display_t *display = output->display;
	igt_pipe_t *pipe;
	int i;
	int ret;
	bool need_wait_for_vblank = false;

	pipe = igt_output_get_driving_pipe(output);

	for (i = 0; i < pipe->n_planes; i++) {
		igt_plane_t *plane = &pipe->planes[i];

		if (plane->fb_changed || plane->position_changed)
			need_wait_for_vblank = true;

		ret = igt_plane_commit(plane, output, s, fail_on_error);
		CHECK_RETURN(ret, fail_on_error);
	}

	/*
	 * If the crtc is enabled, wait until the next vblank before returning
	 * if we made changes to any of the planes.
	 */
	if (need_wait_for_vblank && pipe->enabled) {
		igt_wait_for_vblank(display->drm_fd, pipe->pipe);
	}

	return 0;
}

/*
 * Commit all plane changes across all outputs of the display.
 *
 * If @fail_on_error is true, any failure to commit plane state will lead
 * to subtest failure in the specific function where the failure occurs.
 * Otherwise, the first error code encountered will be returned and no
 * further programming will take place, which may result in some changes
 * taking effect and others not taking effect.
 */
static int do_display_commit(igt_display_t *display,
			     enum igt_commit_style s,
			     bool fail_on_error)
{
	int i, ret;

	LOG_INDENT(display, "commit");

	igt_display_refresh(display);

	for (i = 0; i < display->n_outputs; i++) {
		igt_output_t *output = &display->outputs[i];

		if (!output->valid)
			continue;

		ret = igt_output_commit(output, s, fail_on_error);
		CHECK_RETURN(ret, fail_on_error);
	}

	LOG_UNINDENT(display);

	if (getenv("IGT_DISPLAY_WAIT_AT_COMMIT"))
		igt_wait_for_keypress();

	return 0;
}

/**
 * igt_display_commit2:
 * @display: DRM device handle
 * @s: Commit style
 *
 * Commits framebuffer and positioning changes to all planes of each display
 * pipe, using a specific API to perform the programming.  This function should
 * be used to exercise a specific driver programming API; igt_display_commit
 * should be used instead if the API used is unimportant to the test being run.
 *
 * This function should only be used to commit changes that are expected to
 * succeed, since any failure during the commit process will cause the IGT
 * subtest to fail.  To commit changes that are expected to fail, use
 * @igt_try_display_commit2 instead.
 *
 * Returns: 0 upon success.  This function will never return upon failure
 * since igt_fail() at lower levels will longjmp out of it.
 */
int igt_display_commit2(igt_display_t *display,
		       enum igt_commit_style s)
{
	do_display_commit(display, s, true);

	return 0;
}

/**
 * igt_display_try_commit2:
 * @display: DRM device handle
 * @s: Commit style
 *
 * Attempts to commit framebuffer and positioning changes to all planes of each
 * display pipe.  This function should be used to commit changes that are
 * expected to fail, so that the error code can be checked for correctness.
 * For changes that are expected to succeed, use @igt_display_commit instead.
 *
 * Note that in non-atomic commit styles, no display programming will be
 * performed after the first failure is encountered, so only some of the
 * operations requested by a test may have been completed.  Tests that catch
 * errors returned by this function should take care to restore the display to
 * a sane state after a failure is detected.
 *
 * Returns: 0 upon success, otherwise the error code of the first error
 * encountered.
 */
int igt_display_try_commit2(igt_display_t *display, enum igt_commit_style s)
{
	return do_display_commit(display, s, false);
}

/**
 * igt_display_commit:
 * @display: DRM device handle
 *
 * Commits framebuffer and positioning changes to all planes of each display
 * pipe.
 *
 * Returns: 0 upon success.  This function will never return upon failure
 * since igt_fail() at lower levels will longjmp out of it.
 */
int igt_display_commit(igt_display_t *display)
{
	return igt_display_commit2(display, COMMIT_LEGACY);
}

const char *igt_output_name(igt_output_t *output)
{
	return output->name;
}

drmModeModeInfo *igt_output_get_mode(igt_output_t *output)
{
	return &output->config.default_mode;
}

void igt_output_set_pipe(igt_output_t *output, enum pipe pipe)
{
	igt_display_t *display = output->display;

	if (pipe == PIPE_ANY) {
		LOG(display, "%s: set_pipe(any)\n", igt_output_name(output));
		output->pending_crtc_idx_mask = -1UL;
	} else {
		LOG(display, "%s: set_pipe(%c)\n", igt_output_name(output),
		    pipe_name(pipe));
		output->pending_crtc_idx_mask = 1 << pipe;
	}
}

igt_plane_t *igt_output_get_plane(igt_output_t *output, enum igt_plane plane)
{
	igt_pipe_t *pipe;

	pipe = igt_output_get_driving_pipe(output);
	return igt_pipe_get_plane(pipe, plane);
}

void igt_plane_set_fb(igt_plane_t *plane, struct igt_fb *fb)
{
	igt_pipe_t *pipe = plane->pipe;
	igt_display_t *display = pipe->display;

	LOG(display, "%c.%d: plane_set_fb(%d)\n", pipe_name(pipe->pipe),
	    plane->index, fb ? fb->fb_id : 0);

	plane->fb = fb;

	plane->fb_changed = true;
}

void igt_plane_set_position(igt_plane_t *plane, int x, int y)
{
	igt_pipe_t *pipe = plane->pipe;
	igt_display_t *display = pipe->display;

	LOG(display, "%c.%d: plane_set_position(%d,%d)\n",
	    pipe_name(pipe->pipe), plane->index, x, y);

	plane->crtc_x = x;
	plane->crtc_y = y;

	plane->position_changed = true;
}

void igt_wait_for_vblank(int drm_fd, enum pipe pipe)
{
	drmVBlank wait_vbl;

	memset(&wait_vbl, 0, sizeof(wait_vbl));

	wait_vbl.request.type = pipe << DRM_VBLANK_HIGH_CRTC_SHIFT |
				DRM_VBLANK_RELATIVE;
	wait_vbl.request.sequence = 1;

	igt_assert(drmWaitVBlank(drm_fd, &wait_vbl) == 0);
}
