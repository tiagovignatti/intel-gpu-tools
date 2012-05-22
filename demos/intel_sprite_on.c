/*
 * Copyright 2012 Corporation
 *
 * Author:
 *   Armin Reese <armin.c.reese@intel.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/*
 * This program is intended for testing sprite functionality.
 */
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include "i915_drm.h"
#include "drmtest.h"

#if defined(DRM_IOCTL_MODE_ADDFB2) && defined(DRM_I915_SET_SPRITE_COLORKEY)
#define TEST_PLANES 1
#include "drm_fourcc.h"
#endif

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

struct type_name
{
	int                 type;
	const char          *name;
};

#define type_name_fn(res) \
static const char * res##_str(int type) {			\
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
	{ DRM_MODE_CONNECTOR_DisplayPort, "DisplayPort" },
	{ DRM_MODE_CONNECTOR_HDMIA, "HDMI-A" },
	{ DRM_MODE_CONNECTOR_HDMIB, "HDMI-B" },
	{ DRM_MODE_CONNECTOR_TV, "TV" },
	{ DRM_MODE_CONNECTOR_eDP, "Embedded DisplayPort" },
};

type_name_fn(connector_type)

/*
 * Mode setting with the kernel interfaces is a bit of a chore.
 * First you have to find the connector in question and make sure the
 * requested mode is available.
 * Then you need to find the encoder attached to that connector so you
 * can bind it with a free crtc.
 */
struct connector
{
	uint32_t            id;
	int                 mode_valid;
	drmModeModeInfo     mode;
	drmModeEncoder      *encoder;
	drmModeConnector    *connector;
	int                 crtc;
	int                 pipe;
};

static void dump_mode(
        drmModeModeInfo             *mode)
{
	printf("  %s %d %d %d %d %d %d %d %d %d 0x%x 0x%x %d\n",
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
	       mode->clock);
}

static void dump_connectors(
        int                         gfx_fd,
        drmModeRes                  *resources)
{
	int i, j;

	printf("Connectors:\n");
	printf("id\tencoder\tstatus\t\ttype\tsize (mm)\tmodes\n");
	for (i = 0; i < resources->count_connectors; i++) {
		drmModeConnector *connector;

		connector = drmModeGetConnector(gfx_fd, resources->connectors[i]);
		if (!connector) {
			printf("could not get connector %i: %s\n",
				resources->connectors[i], strerror(errno));
			continue;
		}

		printf("%d\t%d\t%s\t%s\t%dx%d\t\t%d\n",
		       connector->connector_id,
		       connector->encoder_id,
		       connector_status_str(connector->connection),
		       connector_type_str(connector->connector_type),
		       connector->mmWidth, connector->mmHeight,
		       connector->count_modes);

		if (!connector->count_modes)
			continue;

		printf("  modes:\n");
		printf("  name refresh (Hz) hdisp hss hse htot vdisp "
		       "vss vse vtot flags type clock\n");
		for (j = 0; j < connector->count_modes; j++)
			dump_mode(&connector->modes[j]);

		drmModeFreeConnector(connector);
	}
	printf("\n");
}

static void dump_crtcs(
        int                         gfx_fd,
        drmModeRes                  *resources)
{
	int i;

	printf("CRTCs:\n");
	printf("id\tfb\tpos\tsize\n");
	for (i = 0; i < resources->count_crtcs; i++) {
		drmModeCrtc *crtc;

		crtc = drmModeGetCrtc(gfx_fd, resources->crtcs[i]);
		if (!crtc) {
			printf("could not get crtc %i: %s\n",
				   resources->crtcs[i],
				   strerror(errno));
			continue;
		}
		printf("%d\t%d\t(%d,%d)\t(%dx%d)\n",
		       crtc->crtc_id,
		       crtc->buffer_id,
		       crtc->x, crtc->y,
		       crtc->width, crtc->height);
		dump_mode(&crtc->mode);

		drmModeFreeCrtc(crtc);
	}
	printf("\n");
}

static void dump_planes(
        int                         gfx_fd,
        drmModeRes                  *resources)
{
	drmModePlaneRes             *plane_resources;
	drmModePlane                *ovr;
	int i;

	plane_resources = drmModeGetPlaneResources(gfx_fd);
	if (!plane_resources) {
		printf("drmModeGetPlaneResources failed: %s\n",
			   strerror(errno));
		return;
	}

	printf("Planes:\n");
	printf("id\tcrtc\tfb\tCRTC x,y\tx,y\tgamma size\n");
	for (i = 0; i < plane_resources->count_planes; i++) {
		ovr = drmModeGetPlane(gfx_fd, plane_resources->planes[i]);
		if (!ovr) {
			printf("drmModeGetPlane failed: %s\n",
			       strerror(errno));
			continue;
		}

		printf("%d\t%d\t%d\t%d,%d\t\t%d,%d\t%d\n",
		       ovr->plane_id, ovr->crtc_id, ovr->fb_id,
		       ovr->crtc_x, ovr->crtc_y, ovr->x, ovr->y,
		       ovr->gamma_size);

		drmModeFreePlane(ovr);
	}
	printf("\n");

	return;
}

static void connector_find_preferred_mode(
        int                     gfx_fd,
        drmModeRes              *gfx_resources,
        struct connector        *c)
{
	drmModeConnector *connector;
	drmModeEncoder *encoder = NULL;
	int i, j;

	/* First, find the connector & mode */
	c->mode_valid = 0;
	connector = drmModeGetConnector(gfx_fd, c->id);
	if (!connector) {
		printf("could not get connector %d: %s\n",
			   c->id,
			   strerror(errno));
		drmModeFreeConnector(connector);
		return;
	}

	if (connector->connection != DRM_MODE_CONNECTED) {
		drmModeFreeConnector(connector);
		return;
	}

	if (!connector->count_modes) {
		printf("connector %d has no modes\n",
		       c->id);
		drmModeFreeConnector(connector);
		return;
	}

	if (connector->connector_id != c->id) {
		printf("connector id doesn't match (%d != %d)\n",
			    connector->connector_id,
			    c->id);
		drmModeFreeConnector(connector);
		return;
	}

	for (j = 0; j < connector->count_modes; j++) {
		c->mode = connector->modes[j];
		if (c->mode.type & DRM_MODE_TYPE_PREFERRED) {
			c->mode_valid = 1;
			break;
		}
	}

	if (!c->mode_valid) {
		if (connector->count_modes > 0) {
			/* use the first mode as test mode */
			c->mode = connector->modes[0];
			c->mode_valid = 1;
		} else {
			printf("failed to find any modes on connector %d\n",
				   c->id);
			return;
		}
	}

	/* Now get the encoder */
	for (i = 0; i < connector->count_encoders; i++) {
		encoder = drmModeGetEncoder(gfx_fd, connector->encoders[i]);

		if (!encoder) {
			printf("could not get encoder %i: %s\n",
				   gfx_resources->encoders[i],
				   strerror(errno));
			drmModeFreeEncoder(encoder);
			continue;
		}

		break;
	}

	c->encoder = encoder;

	if (i == gfx_resources->count_encoders) {
		printf("failed to find encoder\n");
		c->mode_valid = 0;
		return;
	}

	/* Find first CRTC not in use */
	for (i = 0; i < gfx_resources->count_crtcs; i++) {
		if (gfx_resources->crtcs[i] && (c->encoder->possible_crtcs & (1<<i)))
			break;
	}
	c->crtc = gfx_resources->crtcs[i];
	c->pipe = i;

	gfx_resources->crtcs[i] = 0;

	c->connector = connector;
}

static int connector_find_plane(int gfx_fd, struct connector *c)
{
	drmModePlaneRes *plane_resources;
	drmModePlane *ovr;
	uint32_t id = 0;
	int i;

	plane_resources = drmModeGetPlaneResources(gfx_fd);
	if (!plane_resources) {
		printf("drmModeGetPlaneResources failed: %s\n",
		       strerror(errno));
		return 0;
	}

	for (i = 0; i < plane_resources->count_planes; i++) {
		ovr = drmModeGetPlane(gfx_fd, plane_resources->planes[i]);
		if (!ovr) {
			printf("drmModeGetPlane failed: %s\n",
				   strerror(errno));
			continue;
		}

		if (ovr->possible_crtcs & (1 << c->pipe)) {
			id = ovr->plane_id;
			drmModeFreePlane(ovr);
			break;
		}
		drmModeFreePlane(ovr);
	}

	return id;
}

static int prepare_primary_surface(
            int                     fd,
            int                     prim_width,
            int                     prim_height,
            uint32_t                *prim_handle,
            uint32_t                *prim_stride,
            uint32_t                *prim_size,
            int                     tiled)
{
    uint32_t                        bytes_per_pixel = sizeof(uint32_t);
    uint32_t                        *prim_fb_ptr;
    struct drm_i915_gem_set_tiling  set_tiling;

    if (bytes_per_pixel != sizeof(uint32_t)) {
        printf("Bad bytes_per_pixel for primary surface: %d\n",
               bytes_per_pixel);
        return -EINVAL;
    }

    if (tiled) {
        int                         v;

        /* Round the tiling up to the next power-of-two and the
         * region up to the next pot fence size so that this works
         * on all generations.
         *
         * This can still fail if the framebuffer is too large to
         * be tiled. But then that failure is expected.
         */

        v = prim_width * bytes_per_pixel;
        for (*prim_stride = 512; *prim_stride < v; *prim_stride *= 2)
            ;

        v = *prim_stride * prim_height;
        for (*prim_size = 1024*1024; *prim_size < v; *prim_size *= 2)
            ;
    } else {
        /* Scan-out has a 64 byte alignment restriction */
        *prim_stride = (prim_width * bytes_per_pixel + 63) & ~63;
        *prim_size = *prim_stride * prim_height;
    }

    *prim_handle = gem_create(fd, *prim_size);

    if (tiled) {
        set_tiling.handle = *prim_handle;
        set_tiling.tiling_mode = I915_TILING_X;
        set_tiling.stride = *prim_stride;
        if (ioctl(fd, DRM_IOCTL_I915_GEM_SET_TILING, &set_tiling)) {
            printf("Set tiling failed: %s (stride=%d, size=%d)\n",
                    strerror(errno), *prim_stride, *prim_size);
            return -1;
        }
    }

    prim_fb_ptr = gem_mmap(fd,
                           *prim_handle, *prim_size,
                           PROT_READ | PROT_WRITE);

    if (prim_fb_ptr != NULL) {
        // Write primary surface with gray background
        memset(prim_fb_ptr, 0x3f, *prim_size);
        munmap(prim_fb_ptr, *prim_size);
    }

    return 0;
}

static void fill_sprite(
        int                             sprite_width,
        int                             sprite_height,
        int                             sprite_stride,
        int                             sprite_index,
        void                            *sprite_fb_ptr)
{
    __u32                           *pLinePat0,
                                    *pLinePat1,
                                    *pLinePtr;
    int                             i,
                                    line;
    int                             stripe_width;

    stripe_width = ((sprite_width > 64) &&
                    (sprite_height > 64)) ? (sprite_index + 1) * 8 :
                                            (sprite_index + 1) * 2;

    // Note:  sprite_stride is in bytes.  pLinePat0 and pLinePat1
    //        are both __u32 pointers
    pLinePat0 = sprite_fb_ptr;
    pLinePat1 = pLinePat0 + (stripe_width * (sprite_stride / sizeof(*pLinePat0)));

    for (i = 0; i < sprite_width; i++) {
        *(pLinePat0 + i) = ((i / stripe_width) & 0x1) ? 0 : ~0;
        *(pLinePat1 + i) = ~(*(pLinePat0 + i));
    }

    for (line = 1; line < sprite_height; line++) {
        if (line == stripe_width) {
            continue;
        }

        pLinePtr = ((line / stripe_width) & 0x1) ? pLinePat1 : pLinePat0;
        memcpy( pLinePat0 + ((sprite_stride / sizeof(*pLinePat0)) * line),
                pLinePtr,
                sprite_width * sizeof(*pLinePat0));
    }

    return;
}

static int prepare_sprite_surfaces(
            int                     fd,
            int                     sprite_width,
            int                     sprite_height,
            uint32_t                num_surfaces,
            uint32_t                *sprite_handles,
            uint32_t                *sprite_stride,
            uint32_t                *sprite_size,
            int                     tiled)
{
    uint32_t                        bytes_per_pixel = sizeof(uint32_t);
    uint32_t                        *sprite_fb_ptr;
    struct drm_i915_gem_set_tiling  set_tiling;
    int                             i;

    if (bytes_per_pixel != sizeof(uint32_t)) {
        printf("Bad bytes_per_pixel for sprite: %d\n", bytes_per_pixel);
        return -EINVAL;
    }

    if (tiled) {
        int                         v;

        /* Round the tiling up to the next power-of-two and the
         * region up to the next pot fence size so that this works
         * on all generations.
         *
         * This can still fail if the framebuffer is too large to
         * be tiled. But then that failure is expected.
         */

        v = sprite_width * bytes_per_pixel;
        for (*sprite_stride = 512; *sprite_stride < v; *sprite_stride *= 2)
            ;

        v = *sprite_stride * sprite_height;
        for (*sprite_size = 1024*1024; *sprite_size < v; *sprite_size *= 2)
            ;
    } else {
        /* Scan-out has a 64 byte alignment restriction */
        *sprite_stride = (sprite_width * bytes_per_pixel + 63) & ~63;
        *sprite_size = *sprite_stride * sprite_height;
    }

    for (i = 0; i < num_surfaces;  i++) {
        // Create the sprite surface
        sprite_handles[i] = gem_create(fd, *sprite_size);

        if (tiled) {
            set_tiling.handle = sprite_handles[i];
            set_tiling.tiling_mode = I915_TILING_X;
            set_tiling.stride = *sprite_stride;
            if (ioctl(fd, DRM_IOCTL_I915_GEM_SET_TILING, &set_tiling)) {
                printf("Set tiling failed: %s (stride=%d, size=%d)\n",
                       strerror(errno), *sprite_stride, *sprite_size);
                return -1;
            }
        }

        // Get pointer to the surface
        sprite_fb_ptr = gem_mmap(fd,
                                 sprite_handles[i], *sprite_size,
                                 PROT_READ | PROT_WRITE);

        if (sprite_fb_ptr != NULL) {
            // Fill with checkerboard pattern
            fill_sprite(
                    sprite_width,
                    sprite_height,
                    *sprite_stride,
                    i, sprite_fb_ptr);

            munmap(sprite_fb_ptr, *sprite_size);
        } else {
            i--;
            while (i >= 0) {
                gem_close(fd, sprite_handles[i]);
                i--;
            }
        }
    }

    return 0;
}

static void ricochet(
        int                             tiled,
        int                             sprite_w,
        int                             sprite_h,
        int                             out_w,
        int                             out_h,
        int                             dump_info)
{
    int                                 ret;
    int                                 gfx_fd;
    int                                 keep_moving;
    const int                           num_surfaces = 3;
    uint32_t                            sprite_handles[num_surfaces];
    uint32_t                            sprite_fb_id[num_surfaces];
    int                                 sprite_x;
    int                                 sprite_y;
    uint32_t                            sprite_stride;
    uint32_t                            sprite_size;
    uint32_t                            handles[4],
                                        pitches[4],
                                        offsets[4]; /* we only use [0] */
    uint32_t                            prim_width,
                                        prim_height,
                                        prim_handle,
                                        prim_stride,
                                        prim_size,
                                        prim_fb_id;
    struct drm_intel_sprite_colorkey    set;
    struct connector                    curr_connector;
    drmModeRes                          *gfx_resources;
    struct termios                      orig_term,
                                        curr_term;
    int                                 c_index;
    int                                 sprite_index;
    unsigned int                        sprite_plane_id;
    uint32_t                            plane_flags = 0;
    int                                 delta_x,
                                        delta_y;
    struct timeval                      stTimeVal;
    long long                           currTime,
                                        prevFlipTime,
                                        prevMoveTime,
                                        deltaFlipTime,
                                        deltaMoveTime,
                                        SleepTime;
    char                                key;

    // Open up I915 graphics device
    gfx_fd = drmOpen("i915", NULL);
    if (gfx_fd < 0) {
        printf("Failed to load i915 driver: %s\n", strerror(errno));
        return;
    }

    // Obtain pointer to struct containing graphics resources
    gfx_resources = drmModeGetResources(gfx_fd);
    if (!gfx_resources) {
        printf("drmModeGetResources failed: %s\n", strerror(errno));
        return;
    }

    if (dump_info != 0) {
        dump_connectors(gfx_fd, gfx_resources);
        dump_crtcs(gfx_fd, gfx_resources);
        dump_planes(gfx_fd, gfx_resources);
    }

    // Save previous terminal settings
    if (tcgetattr( 0, &orig_term) != 0) {
        printf("tcgetattr failure: %s\n",
               strerror(errno));
        return;
    }

    // Set up input to return characters immediately
    curr_term = orig_term;
    curr_term.c_lflag &= ~(ICANON | ECHO | ECHONL);
    curr_term.c_cc[VMIN] = 0;       // No minimum number of characters
    curr_term.c_cc[VTIME] = 0 ;     // Return immediately, even if
                                    // nothing has been entered.
    if (tcsetattr( 0, TCSANOW, &curr_term) != 0) {
        printf("tcgetattr failure: %s\n",
               strerror(errno));
        return;
    }

    // Cycle through all connectors and display the flying sprite
    // where there are displays attached and the hardware will support it.
    for (c_index = 0; c_index < gfx_resources->count_connectors; c_index++)  {
        curr_connector.id = gfx_resources->connectors[c_index];

        // Find the native (preferred) display mode
        connector_find_preferred_mode(gfx_fd, gfx_resources, &curr_connector);
        if (curr_connector.mode_valid == 0) {
            printf("No valid preferred mode detected\n");
            goto out;
        }

        // Determine if sprite hardware is available on pipe
        // associated with this connector.
        sprite_plane_id = connector_find_plane(gfx_fd, &curr_connector);
        if (!sprite_plane_id) {
            printf("Failed to find sprite plane on crtc\n");
            goto out;
        }

        // Width and height of preferred mode
        prim_width = curr_connector.mode.hdisplay;
        prim_height = curr_connector.mode.vdisplay;

        // Allocate and fill memory for primary surface
        ret = prepare_primary_surface(
                      gfx_fd,
                      prim_width,
                      prim_height,
                      &prim_handle,
                      &prim_stride,
                      &prim_size,
                      tiled);
        if (ret != 0) {
            printf("Failed to add primary fb (%dx%d): %s\n",
                prim_width, prim_height, strerror(errno));
            goto out;
        }

        // Add the primary surface framebuffer
        ret = drmModeAddFB(
                      gfx_fd,
                      prim_width,
                      prim_height,
                      24, 32,
                      prim_stride,
                      prim_handle,
                      &prim_fb_id);
        gem_close(gfx_fd, prim_handle);

        if (ret != 0) {
            printf("Failed to add primary fb (%dx%d): %s\n",
                prim_width, prim_height, strerror(errno));
            goto out;
        }

        // Allocate and fill sprite surfaces
        ret = prepare_sprite_surfaces(
                      gfx_fd,
                      sprite_w,
                      sprite_h,
                      num_surfaces,
                      &sprite_handles[0],
                      &sprite_stride,
                      &sprite_size,
                      tiled);
        if (ret != 0) {
            printf("Preparation of sprite surfaces failed %dx%d\n",
                    sprite_w, sprite_h);
            goto out;
        }

        // Add the sprite framebuffers
        for (sprite_index = 0; sprite_index < num_surfaces; sprite_index++) {
            handles[0] = sprite_handles[sprite_index];
            handles[1] = handles[0];
            handles[2] = handles[0];
            handles[3] = handles[0];
            pitches[0] = sprite_stride;
            pitches[1] = sprite_stride;
            pitches[2] = sprite_stride;
            pitches[3] = sprite_stride;
            memset(offsets, 0, sizeof(offsets));

            ret = drmModeAddFB2(
                          gfx_fd,
                          sprite_w, sprite_h, DRM_FORMAT_XRGB8888,
                          handles, pitches, offsets, &sprite_fb_id[sprite_index],
                          plane_flags);
            gem_close(gfx_fd, sprite_handles[sprite_index]);

            if (ret) {
                printf("Failed to add sprite fb (%dx%d): %s\n",
                       sprite_w, sprite_h, strerror(errno));

                sprite_index--;
                while (sprite_index >= 0) {
                    drmModeRmFB(gfx_fd, sprite_fb_id[sprite_index]);
                    sprite_index--;
                }
                goto out;
            }
        }

        if (dump_info != 0) {
            printf("Displayed Mode Connector struct:\n"
                   "    .id = %d\n"
                   "    .mode_valid = %d\n"
                   "    .crtc = %d\n"
                   "    .pipe = %d\n"
                   "    drmModeModeInfo ...\n"
                   "        .name = %s\n"
                   "        .type = %d\n"
                   "        .flags = %08x\n"
                   "    drmModeEncoder ...\n"
                   "        .encoder_id = %d\n"
                   "        .encoder_type = %d (%s)\n"
                   "        .crtc_id = %d\n"
                   "        .possible_crtcs = %d\n"
                   "        .possible_clones = %d\n"
                   "    drmModeConnector ...\n"
                   "        .connector_id = %d\n"
                   "        .encoder_id = %d\n"
                   "        .connector_type = %d (%s)\n"
                   "        .connector_type_id = %d\n\n",
                   curr_connector.id,
                   curr_connector.mode_valid,
                   curr_connector.crtc,
                   curr_connector.pipe,
                   curr_connector.mode.name,
                   curr_connector.mode.type,
                   curr_connector.mode.flags,
                   curr_connector.encoder->encoder_id,
                   curr_connector.encoder->encoder_type,
                   encoder_type_str(curr_connector.encoder->encoder_type),
                   curr_connector.encoder->crtc_id,
                   curr_connector.encoder->possible_crtcs,
                   curr_connector.encoder->possible_clones,
                   curr_connector.connector->connector_id,
                   curr_connector.connector->encoder_id,
                   curr_connector.connector->connector_type,
                   connector_type_str(curr_connector.connector->connector_type),
                   curr_connector.connector->connector_type_id);

            printf("Sprite surface dimensions = %dx%d\n"
                   "Sprite output dimensions = %dx%d\n"
                    "Press any key to continue >\n",
                    sprite_w,
                    sprite_h,
                    out_w,
                    out_h);

            // Wait for a key-press
            while( read(0, &key, 1) == 0);
            // Purge unread characters
            tcflush(0, TCIFLUSH);
        }

        // Set up the primary display mode
        ret = drmModeSetCrtc(
                      gfx_fd,
                      curr_connector.crtc,
                      prim_fb_id,
                      0, 0,
                      &curr_connector.id,
                      1,
                      &curr_connector.mode);
        if (ret != 0)
        {
            printf("Failed to set mode (%dx%d@%dHz): %s\n",
                prim_width, prim_height, curr_connector.mode.vrefresh,
                strerror(errno));
            continue;
        }

        // Set the sprite colorkey state
        set.plane_id = sprite_plane_id;
        set.min_value = 0;
        set.max_value = 0;
        set.flags = I915_SET_COLORKEY_NONE;
        ret = drmCommandWrite(gfx_fd, DRM_I915_SET_SPRITE_COLORKEY, &set,
                      sizeof(set));

        // Set up sprite output dimensions, initial position, etc.
        if (out_w > prim_width / 2)
            out_w = prim_width / 2;
        if (out_h > prim_height / 2)
            out_h = prim_height / 2;

        delta_x = 3;
        delta_y = 4;
        sprite_x = (prim_width / 2) - (out_w / 2);
        sprite_y = (prim_height / 2) - (out_h / 2);

        currTime = 0;
        prevFlipTime = 0;       // Will force immediate sprite flip
        prevMoveTime = 0;       // Will force immediate sprite move
        deltaFlipTime = 500000; // Flip sprite surface every 1/2 second
        deltaMoveTime = 100000; // Move sprite every 100 ms
        sprite_index = num_surfaces - 1;
        keep_moving = 1;

        // Bounce sprite off the walls
        while (keep_moving) {
            // Obtain system time in usec.
            if (gettimeofday( &stTimeVal, NULL ) != 0) {
                printf("gettimeofday error: %s\n",
                       strerror(errno));
            } else {
                currTime = ((long long)stTimeVal.tv_sec * 1000000) + stTimeVal.tv_usec;
            }

            // Check if it's time to flip the sprite surface
            if (currTime - prevFlipTime > deltaFlipTime)  {
                sprite_index = (sprite_index + 1) % num_surfaces;

                prevFlipTime = currTime;
            }

            // Move the sprite on the screen and flip
            // the surface if the index has changed
            if (drmModeSetPlane(
                        gfx_fd,
                        sprite_plane_id,
                        curr_connector.crtc,
                        sprite_fb_id[sprite_index],
                        plane_flags,
                        sprite_x, sprite_y,
                        out_w, out_h,
                        0, 0,
                        sprite_w, sprite_h)) {
                printf("Failed to enable sprite plane: %s\n",
                       strerror(errno));
            }

            // Check if it's time to move the sprite surface
            if (currTime - prevMoveTime > deltaMoveTime)  {

                // Compute the next position for sprite
                sprite_x += delta_x;
                sprite_y += delta_y;
                if (sprite_x < 0) {
                    sprite_x = 0;
                    delta_x = -delta_x;
                }
                else if (sprite_x > prim_width - out_w) {
                    sprite_x = prim_width - out_w;
                    delta_x = -delta_x;
                }

                if (sprite_y < 0) {
                    sprite_y = 0;
                    delta_y = -delta_y;
                }
                else if (sprite_y > prim_height - out_h) {
                    sprite_y = prim_height - out_h;
                    delta_y = -delta_y;
                }

                prevMoveTime = currTime;
            }

            // Fetch a key from input (non-blocking)
            if (read(0, &key, 1) == 1) {
                switch (key) {
                case 'q':       // Kill the program
                case 'Q':
                    goto out;
                    break;
                case 's':       // Slow down sprite movement;
                     deltaMoveTime = (deltaMoveTime * 100) / 90;
                   if (deltaMoveTime > 800000) {
                        deltaMoveTime = 800000;
                    }
                    break;
                case 'S':       // Speed up sprite movement;
                    deltaMoveTime = (deltaMoveTime * 100) / 110;
                    if (deltaMoveTime < 2000) {
                        deltaMoveTime = 2000;
                    }
                    break;
                case 'f':       // Slow down sprite flipping;
                    deltaFlipTime = (deltaFlipTime * 100) / 90;
                    if (deltaFlipTime > 1000000) {
                        deltaFlipTime = 1000000;
                    }
                    break;
                case 'F':       // Speed up sprite flipping;
                    deltaFlipTime = (deltaFlipTime * 100) / 110;
                    if (deltaFlipTime < 20000) {
                        deltaFlipTime = 20000;
                    }
                    break;
                case 'n':       // Next connector
                case 'N':
                    keep_moving = 0;
                    break;
                default:
                    break;
                }

                // Purge unread characters
                tcflush(0, TCIFLUSH);
            }

            // Wait for min of flip or move deltas
            SleepTime = (deltaFlipTime < deltaMoveTime) ?
                            deltaFlipTime : deltaMoveTime;
            usleep(SleepTime);
        }
    }

out:
    // Purge unread characters
    tcflush(0, TCIFLUSH);
    // Restore previous terminal settings
    if (tcsetattr( 0, TCSANOW, &orig_term) != 0) {
        printf("tcgetattr failure: %s\n",
               strerror(errno));
        return;
    }

    drmModeFreeResources(gfx_resources);
}

static void usage(char *name)
{
	printf("usage: %s -s <plane width>x<plane height> [-dhto]\n"
           "\t-d\t[optional] dump mode information\n"
           "\t-h\t[optional] output help message\n"
	       "\t-t\t[optional] enable tiling\n"
           "\t-o\t[optional] <output rect width>x<output rect height>\n\n"
           "Keyboard control for sprite movement and flip rate ...\n"
           "\t'q' or 'Q' - Quit the program\n"
           "\t'n' or 'N' - Switch to next display\n"
           "\t's'        - Slow sprite movement\n"
           "\t'S'        - Speed up sprite movement\n"
           "\t'f'        - Slow sprite surface flipping\n"
           "\t'F'        - Speed up sprite surface flipping\n",
           name);
}

int main(int argc, char **argv)
{
	int                 c;
	int                 test_overlay = 0,
	                    enable_tiling = 0,
                        dump_info = 0;
    int                 plane_width = 0,
                        plane_height = 0,
                        out_width = 0,
                        out_height = 0;
	static char         optstr[] = "ds:o:th";

	opterr = 0;
	while ((c = getopt(argc, argv, optstr)) != -1) {
		switch (c) {
        case 'd':               // Dump information
            dump_info = 1;
            break;
		case 't':               // Tiling enable
			enable_tiling = 1;
			break;
		case 's':               // Surface dimensions
            if (sscanf(optarg, "%dx%d",
                       &plane_width, &plane_height) != 2)
                usage(argv[0]);
		    test_overlay = 1;
			break;
        case 'o':               // Output dimensions
            if (sscanf(optarg, "%dx%d",
                       &out_width, &out_height) != 2)
                usage(argv[0]);
            break;
		default:
			printf("unknown option %c\n", c);
			/* fall through */
		case 'h':               // Help!
			usage(argv[0]);
			goto out;
		}
	}

	if (test_overlay) {
	    if (out_width < (plane_width / 2)) {
	        out_width = plane_width;
	    }

	    if (out_height < (plane_height / 2)) {
	        out_height = plane_height;
	    }

	    ricochet(enable_tiling,
	             plane_width,
	             plane_height,
	             out_width,
	             out_height,
                 dump_info);
	} else {
	    printf("Sprite dimensions are required:\n");
	    usage(argv[0]);
	}

out:
	exit(0);
}
