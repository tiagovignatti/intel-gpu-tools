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

#ifndef __IGT_KMS_H__
#define __IGT_KMS_H__

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <xf86drmMode.h>

#include "igt_fb.h"
#include "ioctl_wrappers.h"

/* Low-level helpers with kmstest_ prefix */

enum pipe {
        PIPE_ANY = -1,
        PIPE_A = 0,
        PIPE_B,
        PIPE_C,
        I915_MAX_PIPES
};
const char *kmstest_pipe_name(enum pipe pipe);

/* We namespace this enum to not conflict with the Android i915_drm.h */
enum igt_plane {
        IGT_PLANE_1 = 0,
        IGT_PLANE_PRIMARY = IGT_PLANE_1,
        IGT_PLANE_2,
        IGT_PLANE_3,
        IGT_PLANE_CURSOR,
};

const char *kmstest_plane_name(enum igt_plane plane);

enum port {
        PORT_A = 0,
        PORT_B,
        PORT_C,
        PORT_D,
        PORT_E,
        I915_MAX_PORTS
};

/**
 * kmstest_port_name:
 * @port: display plane
 *
 * Returns: String representing @port, e.g. "A".
 */
#define kmstest_port_name(port) ((port) + 'A')

/**
 * kmstest_encoder_type_str:
 * @type: DRM_MODE_ENCODER_* enumeration value
 *
 * Returns: A string representing the drm encoder @type.
 */
const char *kmstest_encoder_type_str(int type);

/**
 * kmstest_connector_status_str:
 * @status: DRM_MODE_* connector status value
 *
 * Returns: A string representing the drm connector status @status.
 */
const char *kmstest_connector_status_str(int status);

/**
 * kmstest_connector_type_str:
 * @type: DRM_MODE_CONNECTOR_* enumeration value
 *
 * Returns: A string representing the drm connector @type.
 */
const char *kmstest_connector_type_str(int type);

void kmstest_dump_mode(drmModeModeInfo *mode);

int kmstest_get_pipe_from_crtc_id(int fd, int crtc_id);
void kmstest_set_vt_graphics_mode(void);
void kmstest_restore_vt_mode(void);

enum igt_atomic_crtc_properties {
       IGT_CRTC_BACKGROUND = 0,
       IGT_CRTC_CTM,
       IGT_CRTC_DEGAMMA_LUT,
       IGT_CRTC_GAMMA_LUT,
       IGT_NUM_CRTC_PROPS
};

enum igt_atomic_connector_properties {
       IGT_CONNECTOR_SCALING_MODE = 0,
       IGT_CONNECTOR_DPMS,
       IGT_NUM_CONNECTOR_PROPS
};

struct kmstest_connector_config {
	drmModeCrtc *crtc;
	drmModeConnector *connector;
	drmModeEncoder *encoder;
	drmModeModeInfo default_mode;
	uint64_t connector_scaling_mode;
	bool connector_scaling_mode_changed;
	uint64_t connector_dpms;
	bool connector_dpms_changed;
	uint32_t atomic_props_crtc[IGT_NUM_CRTC_PROPS];
	uint32_t atomic_props_connector[IGT_NUM_CONNECTOR_PROPS];
	int crtc_idx;
	int pipe;
};

/**
 * kmstest_force_connector_state:
 * @FORCE_CONNECTOR_UNSPECIFIED: Unspecified
 * @FORCE_CONNECTOR_ON: On
 * @FORCE_CONNECTOR_DIGITAL: Digital
 * @FORCE_CONNECTOR_OFF: Off
 */
enum kmstest_force_connector_state {
	FORCE_CONNECTOR_UNSPECIFIED,
	FORCE_CONNECTOR_ON,
	FORCE_CONNECTOR_DIGITAL,
	FORCE_CONNECTOR_OFF
};

bool kmstest_force_connector(int fd, drmModeConnector *connector,
			     enum kmstest_force_connector_state state);
void kmstest_edid_add_3d(const unsigned char *edid, size_t length, unsigned char *new_edid_ptr[], size_t *new_length);
void kmstest_force_edid(int drm_fd, drmModeConnector *connector,
			const unsigned char *edid, size_t length);

bool kmstest_get_connector_default_mode(int drm_fd, drmModeConnector *connector,
					drmModeModeInfo *mode);
bool kmstest_get_connector_config(int drm_fd, uint32_t connector_id,
				  unsigned long crtc_idx_mask,
				  struct kmstest_connector_config *config);
bool kmstest_probe_connector_config(int drm_fd, uint32_t connector_id,
				    unsigned long crtc_idx_mask,
				    struct kmstest_connector_config *config);
void kmstest_free_connector_config(struct kmstest_connector_config *config);

void kmstest_set_connector_dpms(int fd, drmModeConnector *connector, int mode);
bool kmstest_get_property(int drm_fd, uint32_t object_id, uint32_t object_type,
			  const char *name, uint32_t *prop_id, uint64_t *value,
			  drmModePropertyPtr *prop);
void kmstest_unset_all_crtcs(int drm_fd, drmModeResPtr resources);
int kmstest_get_crtc_idx(drmModeRes *res, uint32_t crtc_id);
uint32_t kmstest_find_crtc_for_connector(int fd, drmModeRes *res,
					 drmModeConnector *connector,
					 uint32_t crtc_blacklist_idx_mask);

/*
 * A small modeset API
 */

/* High-level kms api with igt_ prefix */
enum igt_commit_style {
	COMMIT_LEGACY = 0,
	COMMIT_UNIVERSAL,
	COMMIT_ATOMIC,
};

enum igt_atomic_plane_properties {
       IGT_PLANE_SRC_X = 0,
       IGT_PLANE_SRC_Y,
       IGT_PLANE_SRC_W,
       IGT_PLANE_SRC_H,

       IGT_PLANE_CRTC_X,
       IGT_PLANE_CRTC_Y,
       IGT_PLANE_CRTC_W,
       IGT_PLANE_CRTC_H,

       IGT_PLANE_FB_ID,
       IGT_PLANE_CRTC_ID,
       IGT_PLANE_TYPE,
       IGT_PLANE_ROTATION,
       IGT_NUM_PLANE_PROPS
};


typedef struct igt_display igt_display_t;
typedef struct igt_pipe igt_pipe_t;
typedef uint32_t igt_fixed_t;			/* 16.16 fixed point */

typedef enum {
	/* this maps to the kernel API */
	IGT_ROTATION_0   = 1 << 0,
	IGT_ROTATION_90  = 1 << 1,
	IGT_ROTATION_180 = 1 << 2,
	IGT_ROTATION_270 = 1 << 3,
} igt_rotation_t;

typedef struct {
	/*< private >*/
	igt_pipe_t *pipe;
	int index;
	/* capabilities */
	unsigned int is_primary       : 1;
	unsigned int is_cursor        : 1;
	/* state tracking */
	unsigned int fb_changed       : 1;
	unsigned int position_changed : 1;
	unsigned int panning_changed  : 1;
	unsigned int rotation_changed : 1;
	unsigned int size_changed     : 1;
	/*
	 * drm_plane can be NULL for primary and cursor planes (when not
	 * using the atomic modeset API)
	 */
	drmModePlane *drm_plane;
	struct igt_fb *fb;

	uint32_t rotation_property;

	/* position within pipe_src_w x pipe_src_h */
	int crtc_x, crtc_y;
	/* size within pipe_src_w x pipe_src_h */
	int crtc_w, crtc_h;
	/* panning offset within the fb */
	unsigned int pan_x, pan_y;
	igt_rotation_t rotation;
	uint32_t atomic_props_plane[IGT_NUM_PLANE_PROPS];
} igt_plane_t;

struct igt_pipe {
	igt_display_t *display;
	enum pipe pipe;
	bool enabled;
#define IGT_MAX_PLANES	4
	int n_planes;
	igt_plane_t planes[IGT_MAX_PLANES];
	uint64_t background; /* Background color MSB BGR 16bpc LSB */
	uint32_t background_changed : 1;
	uint32_t background_property;

	uint64_t degamma_blob;
	uint32_t degamma_property;
	uint64_t ctm_blob;
	uint32_t ctm_property;
	uint64_t gamma_blob;
	uint32_t gamma_property;
	uint32_t color_mgmt_changed : 1;

	uint32_t crtc_id;
};

typedef struct {
	/*< private >*/
	igt_display_t *display;
	uint32_t id;					/* KMS id */
	struct kmstest_connector_config config;
	char *name;
	bool valid;
	unsigned long pending_crtc_idx_mask;
	bool use_override_mode;
	drmModeModeInfo override_mode;
} igt_output_t;

struct igt_display {
	int drm_fd;
	int log_shift;
	int n_pipes;
	int n_outputs;
	unsigned long pipes_in_use;
	igt_output_t *outputs;
	igt_pipe_t pipes[I915_MAX_PIPES];
	bool has_universal_planes;
	bool is_atomic;
};

void igt_display_init(igt_display_t *display, int drm_fd);
void igt_display_fini(igt_display_t *display);
int  igt_display_commit2(igt_display_t *display, enum igt_commit_style s);
int  igt_display_commit(igt_display_t *display);
int  igt_display_try_commit2(igt_display_t *display, enum igt_commit_style s);
int  igt_display_get_n_pipes(igt_display_t *display);

const char *igt_output_name(igt_output_t *output);
drmModeModeInfo *igt_output_get_mode(igt_output_t *output);
void igt_output_override_mode(igt_output_t *output, drmModeModeInfo *mode);
void igt_output_set_pipe(igt_output_t *output, enum pipe pipe);
igt_plane_t *igt_output_get_plane(igt_output_t *output, enum igt_plane plane);
bool igt_pipe_get_property(igt_pipe_t *pipe, const char *name,
			   uint32_t *prop_id, uint64_t *value,
			   drmModePropertyPtr *prop);

static inline bool igt_plane_supports_rotation(igt_plane_t *plane)
{
	return plane->rotation_property != 0;
}

void igt_pipe_set_degamma_lut(igt_pipe_t *pipe, void *ptr, size_t length);
void igt_pipe_set_ctm_matrix(igt_pipe_t *pipe, void *ptr, size_t length);
void igt_pipe_set_gamma_lut(igt_pipe_t *pipe, void *ptr, size_t length);

void igt_plane_set_fb(igt_plane_t *plane, struct igt_fb *fb);
void igt_plane_set_position(igt_plane_t *plane, int x, int y);
void igt_plane_set_size(igt_plane_t *plane, int w, int h);
void igt_plane_set_panning(igt_plane_t *plane, int x, int y);
void igt_plane_set_rotation(igt_plane_t *plane, igt_rotation_t rotation);
void igt_crtc_set_background(igt_pipe_t *pipe, uint64_t background);
void igt_fb_set_position(struct igt_fb *fb, igt_plane_t *plane,
	uint32_t x, uint32_t y);
void igt_fb_set_size(struct igt_fb *fb, igt_plane_t *plane,
	uint32_t w, uint32_t h);

void igt_wait_for_vblank(int drm_fd, enum pipe pipe);

#define for_each_connected_output(display, output)		\
	for (int i__ = 0;  i__ < (display)->n_outputs; i__++)	\
		if ((output = &(display)->outputs[i__]), output->valid)

#define for_each_pipe(display, pipe)					\
	for (pipe = 0; pipe < igt_display_get_n_pipes(display); pipe++)	\

#define for_each_plane_on_pipe(display, pipe, plane)			\
	for (int j__ = 0; (plane) = &(display)->pipes[(pipe)].planes[j__], \
		     j__ < (display)->pipes[(pipe)].n_planes; j__++)

#define IGT_FIXED(i,f)	((i) << 16 | (f))

/**
 * igt_atomic_populate_plane_req:
 * @req: A pointer to drmModeAtomicReq
 * @plane: A pointer igt_plane_t
 * @prop: one of igt_atomic_plane_properties
 * @value: the value to add
 */
#define igt_atomic_populate_plane_req(req, plane, prop, value) \
	igt_assert_lt(0, drmModeAtomicAddProperty(req, plane->drm_plane->plane_id,\
						  plane->atomic_props_plane[prop], value))

/**
 * igt_atomic_populate_crtc_req:
 * @req: A pointer to drmModeAtomicReq
 * @output: A pointer igt_output_t
 * @prop: one of igt_atomic_crtc_properties
 * @value: the value to add
 */
#define igt_atomic_populate_crtc_req(req, output, prop, value) \
	igt_assert_lt(0, drmModeAtomicAddProperty(req, output->config.crtc->crtc_id,\
						  output->config.atomic_props_crtc[prop], value))
/**
 * igt_atomic_populate_connector_req:
 * @req: A pointer to drmModeAtomicReq
 * @output: A pointer igt_output_t
 * @prop: one of igt_atomic_connector_properties
 * @value: the value to add
 */
#define igt_atomic_populate_connector_req(req, output, prop, value) \
	igt_assert_lt(0, drmModeAtomicAddProperty(req, output->config.connector->connector_id,\
						  output->config.atomic_props_connector[prop], value))

void igt_enable_connectors(void);
void igt_reset_connectors(void);

#define EDID_LENGTH 128
const unsigned char* igt_kms_get_base_edid(void);
const unsigned char* igt_kms_get_alt_edid(void);


#endif /* __IGT_KMS_H__ */
