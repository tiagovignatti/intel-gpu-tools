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
	void (*position)(struct overlay *, enum position);
	void (*hide)(struct overlay *);
};

extern const cairo_user_data_key_t overlay_key;

cairo_surface_t *x11_overlay_create(enum position pos, int *width, int *height);
void x11_overlay_stop(void);
