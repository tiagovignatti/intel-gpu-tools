#include <X11/Xlib.h>
#include <X11/extensions/Xvlib.h>
#include <cairo.h>
#include <stdint.h>

void rgb2yuv_init(void);
int rgb2yuv(cairo_surface_t *rgb, XvImage *image, uint8_t *yuv);
