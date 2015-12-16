/*
 * Copyright © 2015 Intel Corporation
 * Copyright © 2014-2015 Collabora, Ltd.
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
 *    Micah Fedke <micah.fedke@collabora.co.uk>
 *    Daniel Stone <daniels@collabora.com>
 *    Pekka Paalanen <pekka.paalanen@collabora.co.uk>
 */

/*
 * Testcase: testing atomic modesetting API
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <xf86drmMode.h>
#include <cairo.h>
#include "drm.h"
#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "igt.h"
#include "igt_aux.h"

#ifndef DRM_CLIENT_CAP_ATOMIC
#define DRM_CLIENT_CAP_ATOMIC 3
#endif

#ifndef DRM_CAP_CURSOR_WIDTH
#define DRM_CAP_CURSOR_WIDTH 0x8
#endif

#ifndef DRM_CAP_CURSOR_HEIGHT
#define DRM_CAP_CURSOR_HEIGHT 0x9
#endif

#ifndef DRM_MODE_ATOMIC_TEST_ONLY
#define DRM_MODE_ATOMIC_TEST_ONLY 0x0100
#define DRM_MODE_ATOMIC_NONBLOCK 0x0200
#define DRM_MODE_ATOMIC_ALLOW_MODESET 0x0400

struct drm_mode_atomic {
	__u32 flags;
	__u32 count_objs;
	__u64 objs_ptr;
	__u64 count_props_ptr;
	__u64 props_ptr;
	__u64 prop_values_ptr;
	__u64 reserved;
	__u64 user_data;
};
#endif

IGT_TEST_DESCRIPTION("Test atomic modesetting API");

enum kms_atomic_check_relax {
	ATOMIC_RELAX_NONE = 0,
	CRTC_RELAX_MODE = (1 << 0),
	PLANE_RELAX_FB = (1 << 1)
};

/**
 * KMS plane type enum
 *
 * KMS plane types are represented by enums, which do not have stable numeric
 * values, but must be looked up by their string value each time.
 *
 * To make the code more simple, we define a plane_type enum which maps to
 * each KMS enum value. These values must be looked up through the map, and
 * cannot be passed directly to KMS functions.
 */
enum plane_type {
	PLANE_TYPE_PRIMARY = 0,
	PLANE_TYPE_OVERLAY,
	PLANE_TYPE_CURSOR,
	NUM_PLANE_TYPE_PROPS
};

static const char *plane_type_prop_names[NUM_PLANE_TYPE_PROPS] = {
	"Primary",
	"Overlay",
	"Cursor"
};

enum plane_properties {
	PLANE_SRC_X = 0,
	PLANE_SRC_Y,
	PLANE_SRC_W,
	PLANE_SRC_H,
	PLANE_CRTC_X,
	PLANE_CRTC_Y,
	PLANE_CRTC_W,
	PLANE_CRTC_H,
	PLANE_FB_ID,
	PLANE_CRTC_ID,
	PLANE_TYPE,
	NUM_PLANE_PROPS
};

static const char *plane_prop_names[NUM_PLANE_PROPS] = {
	"SRC_X",
	"SRC_Y",
	"SRC_W",
	"SRC_H",
	"CRTC_X",
	"CRTC_Y",
	"CRTC_W",
	"CRTC_H",
	"FB_ID",
	"CRTC_ID",
	"type"
};

enum crtc_properties {
	CRTC_MODE_ID = 0,
	CRTC_ACTIVE,
	NUM_CRTC_PROPS
};

static const char *crtc_prop_names[NUM_CRTC_PROPS] = {
	"MODE_ID",
	"ACTIVE"
};

enum connector_properties {
	CONNECTOR_CRTC_ID = 0,
	NUM_CONNECTOR_PROPS
};

static const char *connector_prop_names[NUM_CONNECTOR_PROPS] = {
	"CRTC_ID"
};

struct kms_atomic_blob {
	uint32_t id; /* 0 if not already allocated */
	size_t len;
	void *data;
};

struct kms_atomic_connector_state {
	struct kms_atomic_state *state;
	uint32_t obj;
	uint32_t crtc_id;
};

struct kms_atomic_plane_state {
	struct kms_atomic_state *state;
	uint32_t obj;
	enum plane_type type;
	uint32_t crtc_mask;
	uint32_t crtc_id; /* 0 to disable */
	uint32_t fb_id; /* 0 to disable */
	uint32_t src_x, src_y, src_w, src_h; /* 16.16 fixed-point */
	uint32_t crtc_x, crtc_y, crtc_w, crtc_h; /* normal integers */
};

struct kms_atomic_crtc_state {
	struct kms_atomic_state *state;
	uint32_t obj;
	int idx;
	bool active;
	struct kms_atomic_blob mode;
};

struct kms_atomic_state {
	struct kms_atomic_connector_state *connectors;
	int num_connectors;
	struct kms_atomic_crtc_state *crtcs;
	int num_crtcs;
	struct kms_atomic_plane_state *planes;
	int num_planes;
	struct kms_atomic_desc *desc;
};

struct kms_atomic_desc {
	int fd;
	uint32_t props_connector[NUM_CONNECTOR_PROPS];
	uint32_t props_crtc[NUM_CRTC_PROPS];
	uint32_t props_plane[NUM_PLANE_PROPS];
	uint64_t props_plane_type[NUM_PLANE_TYPE_PROPS];
};

static uint32_t blob_duplicate(int fd, uint32_t id_orig)
{
	drmModePropertyBlobPtr orig = drmModeGetPropertyBlob(fd, id_orig);
	uint32_t id_new;

	igt_assert(orig);
	do_or_die(drmModeCreatePropertyBlob(fd, orig->data, orig->length,
					    &id_new));
	drmModeFreePropertyBlob(orig);

	return id_new;
}

#define crtc_set_prop(req, crtc, prop, value) \
	igt_assert_lt(0, drmModeAtomicAddProperty(req, crtc->obj, \
						  crtc->state->desc->props_crtc[prop], \
						  value));

#define plane_set_prop(req, plane, prop, value) \
	igt_assert_lt(0, drmModeAtomicAddProperty(req, plane->obj, \
						  plane->state->desc->props_plane[prop], \
						  value));

#define do_atomic_commit(fd, req, flags) \
	do_or_die(drmModeAtomicCommit(fd, req, flags, NULL));

#define do_atomic_commit_err(fd, req, flags, err) { \
	igt_assert_neq(drmModeAtomicCommit(fd, req, flags, NULL), 0); \
	igt_assert_eq(errno, err); \
}

#define crtc_commit_atomic(crtc, plane, req, relax) { \
	drmModeAtomicSetCursor(req, 0); \
	crtc_populate_req(crtc, req); \
	plane_populate_req(plane, req); \
	do_atomic_commit((crtc)->state->desc->fd, req, 0); \
	crtc_check_current_state(crtc, plane, relax); \
	plane_check_current_state(plane, relax); \
}

#define crtc_commit_atomic_err(crtc, plane, crtc_old, plane_old, req, relax, e) { \
	drmModeAtomicSetCursor(req, 0); \
	crtc_populate_req(crtc, req); \
	plane_populate_req(plane, req); \
	do_atomic_commit_err((crtc)->state->desc->fd, req, 0, e); \
	crtc_check_current_state(crtc_old, plane_old, relax); \
	plane_check_current_state(plane_old, relax); \
}

#define plane_commit_atomic(plane, req, relax) { \
	drmModeAtomicSetCursor(req, 0); \
	plane_populate_req(plane, req); \
	do_atomic_commit((plane)->state->desc->fd, req, 0); \
	plane_check_current_state(plane, relax); \
}

#define plane_commit_atomic_err(plane, plane_old, req, relax, e) { \
	drmModeAtomicSetCursor(req, 0); \
	plane_populate_req(plane, req); \
	do_atomic_commit_err((plane)->state->desc->fd, req, 0, e); \
	plane_check_current_state(plane_old, relax); \
}

static void
connector_get_current_state(struct kms_atomic_connector_state *connector)
{
	drmModeObjectPropertiesPtr props;
	int i;

	props = drmModeObjectGetProperties(connector->state->desc->fd,
					   connector->obj,
					   DRM_MODE_OBJECT_CONNECTOR);
	igt_assert(props);

	for (i = 0; i < props->count_props; i++) {
		uint32_t *prop_ids = connector->state->desc->props_connector;

		if (props->props[i] == prop_ids[CONNECTOR_CRTC_ID])
			connector->crtc_id = props->prop_values[i];
	}
	drmModeFreeObjectProperties(props);
}

#if 0
/* XXX: Checking this repeatedly actually hangs the GPU. I have literally no
 *      idea why. */
static void
connector_check_current_state(struct kms_atomic_connector_state *connector)
{
	struct kms_atomic_connector_state connector_kernel;
	drmModeConnectorPtr legacy;
	uint32_t crtc_id;

	legacy = drmModeGetConnectorCurrent(connector->state->desc->fd,
					    connector->obj);
	igt_assert(legacy);

	if (legacy->encoder_id) {
		drmModeEncoderPtr legacy_enc;

		legacy_enc = drmModeGetEncoder(connector->state->desc->fd,
					       legacy->encoder_id);
		igt_assert(legacy_enc);

		crtc_id = legacy_enc->crtc_id;
		drmModeFreeEncoder(legacy_enc);
	} else {
		crtc_id = 0;
	}

	igt_assert_eq_u32(crtc_id, connector->crtc_id);

	memcpy(&connector_kernel, connector, sizeof(connector_kernel));
	connector_get_current_state(&connector_kernel);
	do_or_die(memcmp(&connector_kernel, connector,
			 sizeof(connector_kernel)));

	drmModeFreeConnector(legacy);
}
#endif

static struct kms_atomic_connector_state *
find_connector(struct kms_atomic_state *state,
	       struct kms_atomic_crtc_state *crtc)
{
	int i;

	for (i = 0; i < state->num_connectors; i++) {
		struct kms_atomic_connector_state *connector =
			&state->connectors[i];

		if (!connector->obj)
			continue;
		if (crtc && connector->crtc_id != crtc->obj)
			continue;

		return connector;
	}

	return NULL;
}

static void plane_populate_req(struct kms_atomic_plane_state *plane,
			       drmModeAtomicReq *req)
{
	plane_set_prop(req, plane, PLANE_CRTC_ID, plane->crtc_id);
	plane_set_prop(req, plane, PLANE_FB_ID, plane->fb_id);
	plane_set_prop(req, plane, PLANE_SRC_X, plane->src_x);
	plane_set_prop(req, plane, PLANE_SRC_Y, plane->src_y);
	plane_set_prop(req, plane, PLANE_SRC_W, plane->src_w);
	plane_set_prop(req, plane, PLANE_SRC_H, plane->src_h);
	plane_set_prop(req, plane, PLANE_CRTC_X, plane->crtc_x);
	plane_set_prop(req, plane, PLANE_CRTC_Y, plane->crtc_y);
	plane_set_prop(req, plane, PLANE_CRTC_W, plane->crtc_w);
	plane_set_prop(req, plane, PLANE_CRTC_H, plane->crtc_h);
}

static void plane_get_current_state(struct kms_atomic_plane_state *plane)
{
	struct kms_atomic_desc *desc = plane->state->desc;
	drmModeObjectPropertiesPtr props;
	int i;

	props = drmModeObjectGetProperties(desc->fd, plane->obj,
					   DRM_MODE_OBJECT_PLANE);
	igt_assert(props);

	for (i = 0; i < props->count_props; i++) {
		uint32_t *prop_ids = desc->props_plane;

		if (props->props[i] == prop_ids[PLANE_CRTC_ID])
			plane->crtc_id = props->prop_values[i];
		else if (props->props[i] == prop_ids[PLANE_FB_ID])
			plane->fb_id = props->prop_values[i];
		else if (props->props[i] == prop_ids[PLANE_CRTC_X])
			plane->crtc_x = props->prop_values[i];
		else if (props->props[i] == prop_ids[PLANE_CRTC_Y])
			plane->crtc_y = props->prop_values[i];
		else if (props->props[i] == prop_ids[PLANE_CRTC_W])
			plane->crtc_w = props->prop_values[i];
		else if (props->props[i] == prop_ids[PLANE_CRTC_H])
			plane->crtc_h = props->prop_values[i];
		else if (props->props[i] == prop_ids[PLANE_SRC_X])
			plane->src_x = props->prop_values[i];
		else if (props->props[i] == prop_ids[PLANE_SRC_Y])
			plane->src_y = props->prop_values[i];
		else if (props->props[i] == prop_ids[PLANE_SRC_W])
			plane->src_w = props->prop_values[i];
		else if (props->props[i] == prop_ids[PLANE_SRC_H])
			plane->src_h = props->prop_values[i];
		else if (props->props[i] == prop_ids[PLANE_TYPE]) {
			int j;

			for (j = 0; j < ARRAY_SIZE(desc->props_plane_type); j++) {
				if (props->prop_values[i] == desc->props_plane_type[j]) {
					plane->type = j;
					break;
				}
			}
		}
	}

	drmModeFreeObjectProperties(props);
}

static void plane_check_current_state(struct kms_atomic_plane_state *plane,
				      enum kms_atomic_check_relax relax)
{
	drmModePlanePtr legacy;
	struct kms_atomic_plane_state plane_kernel;

	legacy = drmModeGetPlane(plane->state->desc->fd, plane->obj);
	igt_assert(legacy);

	igt_assert_eq_u32(legacy->crtc_id, plane->crtc_id);

	if (!(relax & PLANE_RELAX_FB))
		igt_assert_eq_u32(legacy->fb_id, plane->fb_id);

	memcpy(&plane_kernel, plane, sizeof(plane_kernel));
	plane_get_current_state(&plane_kernel);

	/* Legacy cursor ioctls create their own, unknowable, internal
	 * framebuffer which we can't reason about. */
	if (relax & PLANE_RELAX_FB)
		plane_kernel.fb_id = plane->fb_id;
	do_or_die(memcmp(&plane_kernel, plane, sizeof(plane_kernel)));

	drmModeFreePlane(legacy);
}

static void plane_commit_legacy(struct kms_atomic_plane_state *plane,
                                enum kms_atomic_check_relax relax)
{
	do_or_die(drmModeSetPlane(plane->state->desc->fd, plane->obj,
				  plane->crtc_id,
				  plane->fb_id, 0,
				  plane->crtc_x, plane->crtc_y,
				  plane->crtc_w, plane->crtc_h,
				  plane->src_x, plane->src_y,
				  plane->src_w, plane->src_h));
	plane_check_current_state(plane, relax);
}

static struct kms_atomic_plane_state *
find_plane(struct kms_atomic_state *state, enum plane_type type,
	   struct kms_atomic_crtc_state *crtc)
{
	int i;

	for (i = 0; i < state->num_planes; i++) {
		struct kms_atomic_plane_state *plane = &state->planes[i];

		if (!plane->obj)
			continue;
		if (type != NUM_PLANE_TYPE_PROPS && plane->type != type)
			continue;
		if (crtc && !(plane->crtc_mask & (1 << crtc->idx)))
			continue;

		plane_get_current_state(plane);
		return plane;
	}

	return NULL;
}

static void crtc_populate_req(struct kms_atomic_crtc_state *crtc,
			      drmModeAtomicReq *req)
{
	crtc_set_prop(req, crtc, CRTC_MODE_ID, crtc->mode.id);
	crtc_set_prop(req, crtc, CRTC_ACTIVE, crtc->active);
}

static void crtc_get_current_state(struct kms_atomic_crtc_state *crtc)
{
	drmModeObjectPropertiesPtr props;
	int i;

	props = drmModeObjectGetProperties(crtc->state->desc->fd, crtc->obj,
					   DRM_MODE_OBJECT_CRTC);
	igt_assert(props);

	for (i = 0; i < props->count_props; i++) {
		uint32_t *prop_ids = crtc->state->desc->props_crtc;

		if (props->props[i] == prop_ids[CRTC_MODE_ID]) {
			drmModePropertyBlobPtr blob;

			crtc->mode.id = props->prop_values[i];
			if (!crtc->mode.id) {
				crtc->mode.len = 0;
				continue;
			}

			blob = drmModeGetPropertyBlob(crtc->state->desc->fd,
						      crtc->mode.id);
			igt_assert(blob);
			igt_assert_eq_u32(blob->length,
					  sizeof(struct drm_mode_modeinfo));

			if (!crtc->mode.data ||
			    memcmp(crtc->mode.data, blob->data, blob->length) != 0)
				crtc->mode.data = blob->data;
			crtc->mode.len = blob->length;
		}
		else if (props->props[i] == prop_ids[CRTC_ACTIVE]) {
			crtc->active = props->prop_values[i];
		}
	}

	drmModeFreeObjectProperties(props);
}

static void crtc_check_current_state(struct kms_atomic_crtc_state *crtc,
				     struct kms_atomic_plane_state *primary,
				     enum kms_atomic_check_relax relax)
{
	struct kms_atomic_crtc_state crtc_kernel;
	drmModeCrtcPtr legacy;

	legacy = drmModeGetCrtc(crtc->state->desc->fd, crtc->obj);
	igt_assert(legacy);

	igt_assert_eq_u32(legacy->crtc_id, crtc->obj);
	igt_assert_eq_u32(legacy->x, primary->src_x >> 16);
	igt_assert_eq_u32(legacy->y, primary->src_y >> 16);

	if (crtc->active)
		igt_assert_eq_u32(legacy->buffer_id, primary->fb_id);
	else
		igt_assert_eq_u32(legacy->buffer_id, 0);

	if (legacy->mode_valid) {
		igt_assert_neq(legacy->mode_valid, 0);
		igt_assert_eq(crtc->mode.len,
		              sizeof(struct drm_mode_modeinfo));
		do_or_die(memcmp(&legacy->mode, crtc->mode.data,
		                 crtc->mode.len));
		igt_assert_eq(legacy->width, legacy->mode.hdisplay);
		igt_assert_eq(legacy->height, legacy->mode.vdisplay);
	} else {
		igt_assert_eq(legacy->mode_valid, 0);
	}

	memcpy(&crtc_kernel, crtc, sizeof(crtc_kernel));
	crtc_get_current_state(&crtc_kernel);

	if (crtc_kernel.mode.id != 0)
		igt_assert_eq(crtc_kernel.mode.len,
		              sizeof(struct drm_mode_modeinfo));

	/* Optionally relax the check for MODE_ID: using the legacy SetCrtc
	 * API can potentially change MODE_ID even if the mode itself remains
	 * unchanged. */
	if (((relax & CRTC_RELAX_MODE) &&
	    (crtc_kernel.mode.id != crtc->mode.id &&
	     crtc_kernel.mode.id != 0 && crtc->mode.id != 0)) &&
	    memcmp(crtc_kernel.mode.data, crtc->mode.data,
		   sizeof(struct drm_mode_modeinfo)) == 0) {
		crtc_kernel.mode.id = crtc->mode.id;
		crtc_kernel.mode.data = crtc->mode.data;
	}

	do_or_die(memcmp(&crtc_kernel, crtc, sizeof(crtc_kernel)));

	drmModeFreeCrtc(legacy);
}

static void crtc_commit_legacy(struct kms_atomic_crtc_state *crtc,
			       struct kms_atomic_plane_state *plane,
			       enum kms_atomic_check_relax relax)
{
	drmModeObjectPropertiesPtr props;
	uint32_t *connectors;
	int num_connectors = 0;
	int i;

	if (!crtc->active) {
		do_or_die(drmModeSetCrtc(crtc->state->desc->fd,
					 crtc->obj, 0, 0, 0, NULL, 0, NULL));
		return;
	}

	connectors = calloc(crtc->state->num_connectors,
			    sizeof(*connectors));
	igt_assert(connectors);

	igt_assert_neq_u32(crtc->mode.id, 0);

	for (i = 0; i < crtc->state->num_connectors; i++) {
		struct kms_atomic_connector_state *connector =
			&crtc->state->connectors[i];

		if (connector->crtc_id != crtc->obj)
			continue;

		connectors[num_connectors++] = connector->obj;
	}

	do_or_die(drmModeSetCrtc(crtc->state->desc->fd, crtc->obj,
	                         plane->fb_id,
				 plane->src_x >> 16, plane->src_y >> 16,
				 (num_connectors) ? connectors : NULL,
				 num_connectors,
				 crtc->mode.data));
	/* When doing a legacy commit, the core may update MODE_ID to be a new
	 * blob implicitly created by the legacy request. Hence we backfill
	 * the value in the state object to ensure they match. */
	props = drmModeObjectGetProperties(crtc->state->desc->fd, crtc->obj,
					   DRM_MODE_OBJECT_CRTC);
	igt_assert(props);

	for (i = 0; i < props->count_props; i++) {
		if (props->props[i] !=
		    crtc->state->desc->props_crtc[CRTC_MODE_ID])
			continue;
		crtc->mode.id = props->prop_values[i];
		break;
	}

	drmModeFreeObjectProperties(props);

	crtc_check_current_state(crtc, plane, relax);
	plane_check_current_state(plane, relax);
}

static struct kms_atomic_crtc_state *find_crtc(struct kms_atomic_state *state,
					       bool must_be_enabled)
{
	int i;

	for (i = 0; i < state->num_crtcs; i++) {
		struct kms_atomic_crtc_state *crtc = &state->crtcs[i];

		if (!crtc->obj)
			continue;
		if (must_be_enabled && !crtc->active)
			continue;

		crtc_get_current_state(crtc);
		return crtc;
	}

	return NULL;
}

static void fill_obj_props(int fd, uint32_t id, int type, int num_props,
			   const char **prop_names, uint32_t *prop_ids)
{
	drmModeObjectPropertiesPtr props;
	int i, j;

	props = drmModeObjectGetProperties(fd, id, type);
	igt_assert(props);

	for (i = 0; i < props->count_props; i++) {
		drmModePropertyPtr prop =
			drmModeGetProperty(fd, props->props[i]);

		for (j = 0; j < num_props; j++) {
			if (strcmp(prop->name, prop_names[j]) != 0)
				continue;
			prop_ids[j] = props->props[i];
			break;
		}

		drmModeFreeProperty(prop);
	}

	drmModeFreeObjectProperties(props);
}

static void fill_obj_prop_map(int fd, uint32_t id, int type, const char *name,
			      int num_enums, const char **enum_names,
			      uint64_t *enum_ids)
{
	drmModeObjectPropertiesPtr props;
	int i, j, k;

	props = drmModeObjectGetProperties(fd, id, type);
	igt_assert(props);

	for (i = 0; i < props->count_props; i++) {
		drmModePropertyPtr prop =
			drmModeGetProperty(fd, props->props[i]);

		igt_assert(prop);

		if (strcmp(prop->name, name) != 0) {
			drmModeFreeProperty(prop);
			continue;
		}

		for (j = 0; j < prop->count_enums; j++) {
			struct drm_mode_property_enum *e = &prop->enums[j];

			for (k = 0; k < num_enums; k++) {
				if (strcmp(e->name, enum_names[k]) != 0)
					continue;

				enum_ids[k] = e->value;
				break;
			}
		}

		drmModeFreeProperty(prop);
	}
}

static void atomic_setup(struct kms_atomic_state *state)
{
	struct kms_atomic_desc *desc = state->desc;
	drmModeResPtr res;
	drmModePlaneResPtr res_plane;
	int i;

	desc->fd = drm_open_driver_master(DRIVER_INTEL);
	igt_assert_fd(desc->fd);

	do_or_die(drmSetClientCap(desc->fd, DRM_CLIENT_CAP_ATOMIC, 1));

	res = drmModeGetResources(desc->fd);
	res_plane = drmModeGetPlaneResources(desc->fd);
	igt_assert(res);
	igt_assert(res_plane);

	igt_assert_lt(0, res->count_crtcs);
	state->num_crtcs = res->count_crtcs;
	state->crtcs = calloc(state->num_crtcs, sizeof(*state->crtcs));
	igt_assert(state->crtcs);

	igt_assert_lt(0, res_plane->count_planes);
	state->num_planes = res_plane->count_planes;
	state->planes = calloc(state->num_planes, sizeof(*state->planes));
	igt_assert(state->planes);

	igt_assert_lt(0, res->count_connectors);
	state->num_connectors = res->count_connectors;
	state->connectors = calloc(state->num_connectors,
				   sizeof(*state->connectors));
	igt_assert(state->connectors);

	fill_obj_props(desc->fd, res->crtcs[0],
		       DRM_MODE_OBJECT_CRTC, NUM_CRTC_PROPS,
		       crtc_prop_names, desc->props_crtc);

	fill_obj_props(desc->fd, res_plane->planes[0],
		       DRM_MODE_OBJECT_PLANE, NUM_PLANE_PROPS,
		       plane_prop_names, desc->props_plane);
	fill_obj_prop_map(desc->fd, res_plane->planes[0],
			  DRM_MODE_OBJECT_PLANE, "type",
			  NUM_PLANE_TYPE_PROPS, plane_type_prop_names,
			  desc->props_plane_type);

	fill_obj_props(desc->fd, res->connectors[0],
		       DRM_MODE_OBJECT_CONNECTOR, NUM_CONNECTOR_PROPS,
		       connector_prop_names, desc->props_connector);

	for (i = 0; i < state->num_crtcs; i++) {
		struct kms_atomic_crtc_state *crtc = &state->crtcs[i];

		crtc->state = state;
		crtc->obj = res->crtcs[i];
		crtc->idx = i;
		crtc_get_current_state(crtc);

		/* The blob pointed to by MODE_ID could well be transient,
		 * and lose its last reference as we switch away from it.
		 * Duplicate the blob here so we have a reference we know we
		 * own. */
		if (crtc->mode.id != 0)
		    crtc->mode.id = blob_duplicate(desc->fd, crtc->mode.id);
	}

	for (i = 0; i < state->num_planes; i++) {
		drmModePlanePtr plane =
			drmModeGetPlane(desc->fd, res_plane->planes[i]);
		igt_assert(plane);

		state->planes[i].state = state;
		state->planes[i].obj = res_plane->planes[i];
		state->planes[i].crtc_mask = plane->possible_crtcs;
		plane_get_current_state(&state->planes[i]);
	}

	for (i = 0; i < state->num_connectors; i++) {
		state->connectors[i].state = state;
		state->connectors[i].obj = res->connectors[i];
		connector_get_current_state(&state->connectors[i]);
	}

	drmModeFreePlaneResources(res_plane);
	drmModeFreeResources(res);
}

static struct kms_atomic_state *
atomic_state_dup(const struct kms_atomic_state *state)
{
	struct kms_atomic_state *ret = calloc(1, sizeof(*ret));

	igt_assert(ret);
	*ret = *state;

	ret->crtcs = calloc(ret->num_crtcs, sizeof(*ret->crtcs));
	igt_assert(ret->crtcs);
	memcpy(ret->crtcs, state->crtcs, ret->num_crtcs * sizeof(*ret->crtcs));

	ret->planes = calloc(ret->num_planes, sizeof(*ret->planes));
	igt_assert(ret->planes);
	memcpy(ret->planes, state->planes,
	       ret->num_planes * sizeof(*ret->planes));

	ret->connectors = calloc(ret->num_connectors, sizeof(*ret->connectors));
	igt_assert(ret->connectors);
	memcpy(ret->connectors, state->connectors,
	       ret->num_connectors * sizeof(*ret->connectors));

	return ret;
}

static void atomic_state_free(struct kms_atomic_state *state)
{
	free(state->crtcs);
	free(state->planes);
	free(state->connectors);
	free(state);
}

static uint32_t plane_get_igt_format(struct kms_atomic_plane_state *plane)
{
	drmModePlanePtr plane_kms;
	const uint32_t *igt_formats;
	uint32_t ret = 0;
	int num_igt_formats;
	int i;

	plane_kms = drmModeGetPlane(plane->state->desc->fd, plane->obj);
	igt_assert(plane_kms);

	igt_get_all_formats(&igt_formats, &num_igt_formats);
	for (i = 0; i < num_igt_formats; i++) {
		int j;

		for (j = 0; j < plane_kms->count_formats; j++) {
			if (plane_kms->formats[j] == igt_formats[i]) {
				ret = plane_kms->formats[j];
				break;
			}
		}
	}

	drmModeFreePlane(plane_kms);
	return ret;
}

static void plane_overlay(struct kms_atomic_crtc_state *crtc,
			  struct kms_atomic_plane_state *plane_old)
{
	struct drm_mode_modeinfo *mode = crtc->mode.data;
	struct kms_atomic_plane_state plane = *plane_old;
	uint32_t format = plane_get_igt_format(&plane);
	drmModeAtomicReq *req = drmModeAtomicAlloc();
	struct igt_fb fb;

	igt_require(req);
	igt_require(format != 0);

	plane.src_x = 0;
	plane.src_y = 0;
	plane.src_w = (mode->hdisplay / 2) << 16;
	plane.src_h = (mode->vdisplay / 2) << 16;
	plane.crtc_x = mode->hdisplay / 4;
	plane.crtc_y = mode->vdisplay / 4;
	plane.crtc_w = mode->hdisplay / 2;
	plane.crtc_h = mode->vdisplay / 2;
	plane.crtc_id = crtc->obj;
	plane.fb_id = igt_create_pattern_fb(plane.state->desc->fd,
					    plane.crtc_w, plane.crtc_h,
					    format, I915_TILING_NONE, &fb);

	/* Enable the overlay plane using the atomic API, and double-check
	 * state is what we think it should be. */
	plane_commit_atomic(&plane, req, ATOMIC_RELAX_NONE);

	/* Disable the plane and check the state matches the old. */
	plane_commit_atomic(plane_old, req, ATOMIC_RELAX_NONE);

	/* Re-enable the plane through the legacy plane API, and verify through
	 * atomic. */
	plane_commit_legacy(&plane, ATOMIC_RELAX_NONE);

	/* Restore the plane to its original settings through the legacy plane
	 * API, and verify through atomic. */
	plane_commit_legacy(plane_old, ATOMIC_RELAX_NONE);

	drmModeAtomicFree(req);
}

static void plane_primary(struct kms_atomic_crtc_state *crtc,
			  struct kms_atomic_plane_state *plane_old)
{
	struct drm_mode_modeinfo *mode = crtc->mode.data;
	struct kms_atomic_plane_state plane = *plane_old;
	uint32_t format = plane_get_igt_format(&plane);
	drmModeAtomicReq *req = drmModeAtomicAlloc();
	uint32_t *connectors;
	int num_connectors;
	struct igt_fb fb;
	int i;

	connectors = calloc(crtc->state->num_connectors, sizeof(*connectors));
	igt_assert(connectors);

	for (i = 0; i < crtc->state->num_connectors; i++) {
		if (crtc->state->connectors[i].crtc_id == crtc->obj)
			connectors[num_connectors++] =
				crtc->state->connectors[i].obj;
	}

	igt_require(format != 0);

	plane.src_x = 0;
	plane.src_y = 0;
	plane.src_w = mode->hdisplay << 16;
	plane.src_h = mode->vdisplay << 16;
	plane.crtc_x = 0;
	plane.crtc_y = 0;
	plane.crtc_w = mode->hdisplay;
	plane.crtc_h = mode->vdisplay;
	plane.crtc_id = crtc->obj;
	plane.fb_id = igt_create_pattern_fb(plane.state->desc->fd,
					    plane.crtc_w, plane.crtc_h,
					    format, I915_TILING_NONE, &fb);

	/* Flip the primary plane using the atomic API, and double-check
	 * state is what we think it should be. */
	crtc_commit_atomic(crtc, &plane, req, ATOMIC_RELAX_NONE);

	/* Restore the primary plane and check the state matches the old. */
	crtc_commit_atomic(crtc, plane_old, req, ATOMIC_RELAX_NONE);

	/* Re-enable the plane through the legacy CRTC/primary-plane API, and
	 * verify through atomic. */
	crtc_commit_legacy(crtc, &plane, CRTC_RELAX_MODE);

	/* Restore the plane to its original settings through the legacy CRTC
	 * API, and verify through atomic. */
	crtc_commit_legacy(crtc, plane_old, CRTC_RELAX_MODE);

	/* Finally, restore to the original state. */
	crtc_commit_atomic(crtc, plane_old, req, ATOMIC_RELAX_NONE);

	drmModeAtomicFree(req);
}

static void plane_cursor(struct kms_atomic_crtc_state *crtc,
			 struct kms_atomic_plane_state *plane_old)
{
	struct drm_mode_modeinfo *mode = crtc->mode.data;
	struct kms_atomic_plane_state plane = *plane_old;
	drmModeAtomicReq *req = drmModeAtomicAlloc();
	struct igt_fb fb;
	uint64_t width, height;

	igt_assert(req);

	/* Any kernel new enough for atomic, also has the cursor size caps. */
	do_or_die(drmGetCap(plane.state->desc->fd,
	                    DRM_CAP_CURSOR_WIDTH, &width));
	do_or_die(drmGetCap(plane.state->desc->fd,
	                    DRM_CAP_CURSOR_HEIGHT, &height));

	plane.src_x = 0;
	plane.src_y = 0;
	plane.src_w = width << 16;
	plane.src_h = height << 16;
	plane.crtc_x = mode->hdisplay / 2;
	plane.crtc_y = mode->vdisplay / 2;
	plane.crtc_w = width;
	plane.crtc_h = height;
	plane.crtc_id = crtc->obj;
	plane.fb_id = igt_create_color_fb(plane.state->desc->fd,
					  width, height,
					  DRM_FORMAT_ARGB8888,
					  LOCAL_DRM_FORMAT_MOD_NONE,
					  0.0, 0.0, 0.0,
					  &fb);
	igt_assert_neq_u32(plane.fb_id, 0);

	/* Flip the cursor plane using the atomic API, and double-check
	 * state is what we think it should be. */
	plane_commit_atomic(&plane, req, ATOMIC_RELAX_NONE);

	/* Restore the cursor plane and check the state matches the old. */
	plane_commit_atomic(plane_old, req, ATOMIC_RELAX_NONE);

	/* Re-enable the plane through the legacy cursor API, and verify
	 * through atomic. */
	do_or_die(drmModeMoveCursor(plane.state->desc->fd, plane.crtc_id,
				    plane.crtc_x, plane.crtc_y));
	do_or_die(drmModeSetCursor(plane.state->desc->fd, plane.crtc_id,
				   fb.gem_handle, width, height));
	plane_check_current_state(&plane, PLANE_RELAX_FB);

	/* Wiggle. */
	plane.crtc_x -= 16;
	plane.crtc_y -= 16;
	do_or_die(drmModeMoveCursor(plane.state->desc->fd, plane.crtc_id,
				    plane.crtc_x, plane.crtc_y));
	plane_check_current_state(&plane, PLANE_RELAX_FB);

	/* Restore the plane to its original settings through the legacy cursor
	 * API, and verify through atomic. */
	do_or_die(drmModeSetCursor2(plane.state->desc->fd, plane.crtc_id,
				    0, 0, 0, 0, 0));
	plane_check_current_state(plane_old, ATOMIC_RELAX_NONE);

	/* Finally, restore to the original state. */
	plane_commit_atomic(plane_old, req, ATOMIC_RELAX_NONE);

	drmModeAtomicFree(req);
}

static void plane_invalid_params(struct kms_atomic_crtc_state *crtc,
				 struct kms_atomic_plane_state *plane_old,
				 struct kms_atomic_connector_state *conn)
{
	struct drm_mode_modeinfo *mode = crtc->mode.data;
	struct kms_atomic_plane_state plane = *plane_old;
	uint32_t format = plane_get_igt_format(&plane);
	drmModeAtomicReq *req = drmModeAtomicAlloc();
	struct igt_fb fb;

	/* Pass a series of invalid object IDs for the FB ID. */
	plane.fb_id = plane.obj;
	plane_commit_atomic_err(&plane, plane_old, req,
	                        ATOMIC_RELAX_NONE, EINVAL);

	plane.fb_id = crtc->obj;
	plane_commit_atomic_err(&plane, plane_old, req,
	                        ATOMIC_RELAX_NONE, EINVAL);

	plane.fb_id = conn->obj;
	plane_commit_atomic_err(&plane, plane_old, req,
	                        ATOMIC_RELAX_NONE, EINVAL);

	plane.fb_id = crtc->mode.id;
	plane_commit_atomic_err(&plane, plane_old, req,
	                        ATOMIC_RELAX_NONE, EINVAL);

	plane.fb_id = plane_old->fb_id;
	plane_commit_atomic(&plane, req, ATOMIC_RELAX_NONE);

	/* Pass a series of invalid object IDs for the CRTC ID. */
	plane.crtc_id = plane.obj;
	plane_commit_atomic_err(&plane, plane_old, req,
	                        ATOMIC_RELAX_NONE, EINVAL);

	plane.crtc_id = plane.fb_id;
	plane_commit_atomic_err(&plane, plane_old, req,
	                        ATOMIC_RELAX_NONE, EINVAL);

	plane.crtc_id = conn->obj;
	plane_commit_atomic_err(&plane, plane_old, req,
	                        ATOMIC_RELAX_NONE, EINVAL);

	plane.crtc_id = crtc->mode.id;
	plane_commit_atomic_err(&plane, plane_old, req,
	                        ATOMIC_RELAX_NONE, EINVAL);

	plane.crtc_id = plane_old->crtc_id;
	plane_commit_atomic(&plane, req, ATOMIC_RELAX_NONE);

	/* Create a framebuffer too small for the plane configuration. */
	igt_require(format != 0);

	plane.src_x = 0;
	plane.src_y = 0;
	plane.src_w = mode->hdisplay << 16;
	plane.src_h = mode->vdisplay << 16;
	plane.crtc_x = 0;
	plane.crtc_y = 0;
	plane.crtc_w = mode->hdisplay;
	plane.crtc_h = mode->vdisplay;
	plane.crtc_id = crtc->obj;
	plane.fb_id = igt_create_pattern_fb(plane.state->desc->fd,
					    plane.crtc_w - 1, plane.crtc_h - 1,
					    format, I915_TILING_NONE, &fb);

	plane_commit_atomic_err(&plane, plane_old, req,
	                        ATOMIC_RELAX_NONE, ENOSPC);

	/* Restore the primary plane and check the state matches the old. */
	plane_commit_atomic(plane_old, req, ATOMIC_RELAX_NONE);

	drmModeAtomicFree(req);
}

static void crtc_invalid_params(struct kms_atomic_crtc_state *crtc_old,
				struct kms_atomic_plane_state *plane,
				struct kms_atomic_connector_state *conn)
{
	struct kms_atomic_crtc_state crtc = *crtc_old;
	drmModeAtomicReq *req = drmModeAtomicAlloc();

	igt_assert(req);

	/* Pass a series of invalid object IDs for the mode ID. */
	crtc.mode.id = plane->obj;
	crtc_commit_atomic_err(&crtc, plane, crtc_old, plane, req,
	                       ATOMIC_RELAX_NONE, EINVAL);

	crtc.mode.id = crtc.obj;
	crtc_commit_atomic_err(&crtc, plane, crtc_old, plane, req,
	                       ATOMIC_RELAX_NONE, EINVAL);

	crtc.mode.id = conn->obj;
	crtc_commit_atomic_err(&crtc, plane, crtc_old, plane, req,
	                       ATOMIC_RELAX_NONE, EINVAL);

	crtc.mode.id = plane->fb_id;
	crtc_commit_atomic_err(&crtc, plane, crtc_old, plane, req,
	                       ATOMIC_RELAX_NONE, EINVAL);

	crtc.mode.id = crtc_old->mode.id;
	crtc_commit_atomic(&crtc, plane, req, ATOMIC_RELAX_NONE);

	/* Create a blob which is the wrong size to be a valid mode. */
	do_or_die(drmModeCreatePropertyBlob(crtc.state->desc->fd,
					    crtc.mode.data,
					    sizeof(struct drm_mode_modeinfo) - 1,
					    &crtc.mode.id));
	crtc_commit_atomic_err(&crtc, plane, crtc_old, plane, req,
	                       ATOMIC_RELAX_NONE, EINVAL);


	do_or_die(drmModeCreatePropertyBlob(crtc.state->desc->fd,
					    crtc.mode.data,
					    sizeof(struct drm_mode_modeinfo) + 1,
					    &crtc.mode.id));
	crtc_commit_atomic_err(&crtc, plane, crtc_old, plane, req,
	                       ATOMIC_RELAX_NONE, EINVAL);

	/* Restore the CRTC and check the state matches the old. */
	crtc_commit_atomic(crtc_old, plane, req, ATOMIC_RELAX_NONE);

	drmModeAtomicFree(req);
}

/* Abuse the atomic ioctl directly in order to test various invalid conditions,
 * which the libdrm wrapper won't allow us to create. */
static void atomic_invalid_params(struct kms_atomic_crtc_state *crtc,
				  struct kms_atomic_plane_state *plane,
				  struct kms_atomic_connector_state *connector)
{
	struct kms_atomic_desc *desc = crtc->state->desc;
	struct drm_mode_atomic ioc;
	uint32_t obj_raw[16]; /* array of objects (sized by count_objs) */
	uint32_t num_props_raw[16]; /* array of num props per obj (ditto) */
	uint32_t props_raw[256]; /* array of props (sum of count_props) */
	uint64_t values_raw[256]; /* array of values for properties (ditto) */
	int i;

	memset(&ioc, 0, sizeof(ioc));

	/* An empty request should do nothing. */
	do_ioctl(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc);

	for (i = 0; i < ARRAY_SIZE(obj_raw); i++)
		obj_raw[i] = 0;
	for (i = 0; i < ARRAY_SIZE(num_props_raw); i++)
		num_props_raw[i] = 0;
	for (i = 0; i < ARRAY_SIZE(props_raw); i++)
		props_raw[i] = 0;
	for (i = 0; i < ARRAY_SIZE(values_raw); i++)
		values_raw[i] = 0;

	ioc.objs_ptr = (uintptr_t) obj_raw;
	ioc.count_props_ptr = (uintptr_t) num_props_raw;
	ioc.props_ptr = (uintptr_t) props_raw;
	ioc.prop_values_ptr = (uintptr_t) values_raw;

	/* Valid pointers, but still should copy nothing. */
	do_ioctl(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc);

	/* Nonsense flags. */
	ioc.flags = 0xdeadbeef;
	do_ioctl_err(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc, EINVAL);

	/* Specifically forbidden combination. */
	ioc.flags = DRM_MODE_ATOMIC_TEST_ONLY | DRM_MODE_PAGE_FLIP_EVENT;
	do_ioctl_err(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc, EINVAL);

	ioc.flags = 0;
	/* Safety check that flags is reset properly. */
	do_ioctl(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc);

	/* Reserved/MBZ. */
	ioc.reserved = 1;
	do_ioctl_err(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc, EINVAL);
	ioc.reserved = 0;
	do_ioctl(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc);

	/* Zero is not a valid object ID. */
	ioc.count_objs = ARRAY_SIZE(obj_raw);
	do_ioctl_err(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc, ENOENT);

	/* Invalid object type (not a thing we can set properties on). */
	ioc.count_objs = 1;
	obj_raw[0] = crtc->mode.id;
	do_ioctl_err(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc, ENOENT);
	obj_raw[0] = plane->fb_id;
	do_ioctl_err(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc, ENOENT);

	/* Filled object but with no properties; no-op. */
	for (i = 0; i < ARRAY_SIZE(obj_raw); i++)
		obj_raw[i] = crtc->obj;
	do_ioctl(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc);

	/* Pass in all sorts of things other than the property ID. */
	num_props_raw[0] = 1;
	do_ioctl_err(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc, ENOENT);
	props_raw[0] = crtc->obj;
	do_ioctl_err(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc, ENOENT);
	props_raw[0] = plane->obj;
	do_ioctl_err(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc, ENOENT);
	props_raw[0] = connector->obj;
	do_ioctl_err(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc, ENOENT);
	props_raw[0] = crtc->mode.id;
	do_ioctl_err(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc, ENOENT);

	/* Valid property, valid value. */
	for (i = 0; i < ARRAY_SIZE(props_raw); i++) {
		props_raw[i] = desc->props_crtc[CRTC_MODE_ID];
		values_raw[i] = crtc->mode.id;
	}
	do_ioctl(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc);

	/* Setting the same thing multiple times is OK. */
	for (i = 0; i < ARRAY_SIZE(obj_raw); i++)
		num_props_raw[i] = ARRAY_SIZE(props_raw) / ARRAY_SIZE(obj_raw);
	do_ioctl(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc);
	ioc.count_objs = ARRAY_SIZE(obj_raw);
	do_ioctl(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc);

	/* Pass a series of outlandish addresses. */
	ioc.objs_ptr = 0;
	do_ioctl_err(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc, EFAULT);

	ioc.objs_ptr = (uintptr_t) obj_raw;
	ioc.count_props_ptr = 0;
	do_ioctl_err(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc, EFAULT);

	ioc.count_props_ptr = (uintptr_t) num_props_raw;
	ioc.props_ptr = 0;
	do_ioctl_err(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc, EFAULT);

	ioc.props_ptr = (uintptr_t) props_raw;
	ioc.prop_values_ptr = 0;
	do_ioctl_err(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc, EFAULT);

	ioc.prop_values_ptr = (uintptr_t) values_raw;
	do_ioctl(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc);

	/* Attempt to overflow and/or trip various boundary conditions. */
	ioc.count_objs = UINT32_MAX / sizeof(uint32_t);
	do_ioctl_err(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc, ENOENT);

	ioc.count_objs = ARRAY_SIZE(obj_raw);
	ioc.objs_ptr = UINT64_MAX - sizeof(uint32_t);
	do_ioctl_err(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc, EFAULT);
	ioc.count_objs = 1;
	ioc.objs_ptr = UINT64_MAX - sizeof(uint32_t);
	do_ioctl_err(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc, EFAULT);

	num_props_raw[0] = UINT32_MAX / sizeof(uint32_t);
	do_ioctl_err(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc, EFAULT);
	num_props_raw[0] = UINT32_MAX - 1;
	do_ioctl_err(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc, EFAULT);

	for (i = 0; i < ARRAY_SIZE(obj_raw); i++)
		num_props_raw[i] = (UINT32_MAX / ARRAY_SIZE(obj_raw)) + 1;
	do_ioctl_err(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc, EFAULT);
	for (i = 0; i < ARRAY_SIZE(obj_raw); i++)
		num_props_raw[i] = ARRAY_SIZE(props_raw) / ARRAY_SIZE(obj_raw);
	do_ioctl_err(desc->fd, DRM_IOCTL_MODE_ATOMIC, &ioc, EFAULT);
}

igt_main
{
	struct kms_atomic_desc desc;
	struct kms_atomic_state *current;

	memset(&desc, 0, sizeof(desc));

	current = calloc(1, sizeof(*current));
	igt_assert(current);
	current->desc = &desc;

	igt_fixture
		atomic_setup(current);

	igt_subtest("plane_overlay_legacy") {
		struct kms_atomic_state *scratch = atomic_state_dup(current);
		struct kms_atomic_crtc_state *crtc = find_crtc(scratch, true);
		struct kms_atomic_plane_state *plane =
			find_plane(scratch, PLANE_TYPE_OVERLAY, crtc);

		igt_require(crtc);
		igt_require(plane);
		plane_overlay(crtc, plane);
		atomic_state_free(scratch);
	}

	igt_subtest("plane_primary_legacy") {
		struct kms_atomic_state *scratch = atomic_state_dup(current);
		struct kms_atomic_crtc_state *crtc = find_crtc(scratch, true);
		struct kms_atomic_plane_state *plane =
			find_plane(scratch, PLANE_TYPE_PRIMARY, crtc);

		igt_require(crtc);
		igt_require(plane);
		plane_primary(crtc, plane);
		atomic_state_free(scratch);
	}

	igt_subtest("plane_cursor_legacy") {
		struct kms_atomic_state *scratch = atomic_state_dup(current);
		struct kms_atomic_crtc_state *crtc = find_crtc(scratch, true);
		struct kms_atomic_plane_state *plane =
			find_plane(scratch, PLANE_TYPE_CURSOR, crtc);

		igt_require(crtc);
		igt_require(plane);
		plane_cursor(crtc, plane);
		atomic_state_free(scratch);
	}

	igt_subtest("plane_invalid_params") {
		struct kms_atomic_state *scratch = atomic_state_dup(current);
		struct kms_atomic_crtc_state *crtc = find_crtc(scratch, true);
		struct kms_atomic_plane_state *plane =
			find_plane(current, PLANE_TYPE_PRIMARY, crtc);
		struct kms_atomic_connector_state *conn =
			find_connector(scratch, crtc);

		igt_require(crtc);
		igt_require(plane);
		plane_invalid_params(crtc, plane, conn);
		atomic_state_free(scratch);
	}

	igt_subtest("crtc_invalid_params") {
		struct kms_atomic_state *scratch = atomic_state_dup(current);
		struct kms_atomic_crtc_state *crtc = find_crtc(scratch, true);
		struct kms_atomic_plane_state *plane =
			find_plane(scratch, NUM_PLANE_TYPE_PROPS, crtc);
		struct kms_atomic_connector_state *conn =
			find_connector(scratch, crtc);

		igt_require(crtc);
		igt_require(plane);
		igt_require(conn);
		crtc_invalid_params(crtc, plane, conn);
		atomic_state_free(scratch);
	}

	igt_subtest("atomic_invalid_params") {
		struct kms_atomic_state *scratch = atomic_state_dup(current);
		struct kms_atomic_crtc_state *crtc = &scratch->crtcs[0];
		struct kms_atomic_plane_state *plane =
			find_plane(scratch, NUM_PLANE_TYPE_PROPS, crtc);
		struct kms_atomic_connector_state *conn =
			find_connector(scratch, crtc);

		igt_require(plane);
		igt_require(conn);
		atomic_invalid_params(crtc, plane, conn);
		atomic_state_free(scratch);
	}

	atomic_state_free(current);

	igt_fixture
		close(desc.fd);
}
