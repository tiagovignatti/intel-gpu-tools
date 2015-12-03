#define GAMMA(x) (((x) * 100) - 100)

#define MANUFACTURER_ID(a, b, c) (a - '@') << 2 | (b - '@') >> 3, \
				 (b - '@') << 5 | (c - '@')


#define ab(x, y) ((x) & 0xff), ((y) & 0xff), (((x) & 0xf00) >> 4) | (((y) & 0xf00) >> 8)
#define op(ho, hp, vo, vp) ((ho) & 0xff), ((hp) & 0xff), \
		(((vo) & 0xf) << 4) | ((vp) & 0xf), \
		(((ho) & 0x300) >> 2) | (((hp) & 0x300) >> 4) \
		| (((vo) & 0x30) >> 2) | ((vp) & 0x30 >> 4)

static unsigned char EDID_NAME[EDID_LENGTH] = {
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, /* header */
	MANUFACTURER_ID('I', 'G', 'T'),
	/* product code, serial number, week and year of manufacture */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x01, 0x03, /* edid version (1.3) */
	/* basic display parameters */
	/* digital display, maximum horizontal image size, maximum vertical
	 * image size, gamma, features: RGB 4:4:4, native pixel format and
	 * refresh rate in descriptor 1 */
	0x80, HSIZE, VSIZE, GAMMA(2.20), 0x02,
	/* chromaticity coordinates */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* established timings: 640x480 60Hz, 800x600 60Hz, 1024x768 60Hz */
	0x21, 0x08, 0x00,
	/* standard timings */
	0xd1, 0xc0, /* 1920x1080 60Hz */
	0x81, 0xc0, /* 1280x720 60Hz */
	0x61, 0x40, /* 1024x768 60Hz */
	0x45, 0x40, /* 800x600 60Hz */
	0x31, 0x40, /* 640x480 60Hz */
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	/* descriptor 1 (preferred timing) */
	(CLOCK / 10) & 0x00ff, ((CLOCK / 10) & 0xff00) >> 8,
	ab(HACTIVE, HBLANK), ab(VACTIVE, VBLANK),
	op(HOFFSET, HPULSE, VOFFSET, VPULSE),
	ab(HSIZE * 10, VSIZE * 10),
	0x00, 0x00, 0x00,
	/* descriptor 2 (monitor range limits) */
	0x00, 0x00, 0x00, 0xfd, 0x00,
	VFREQ - 1, VFREQ + 1, /* minimum, maximum vertical field rate */
	(CLOCK / (HACTIVE + HBLANK)) - 1, /* minimum horizontal line rate */
	(CLOCK / (HACTIVE + HBLANK)) + 1, /* maximum horizontal line rate */
	(CLOCK / 10000) + 1, /* maximum pixel clock rate */
	0x00, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	/* descriptor 3 (name descriptor) */
	0x00, 0x00, 0x00, 0xfc, 0x00,  'I',  'G',  'T', 0x0a,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	/* descriptor 4 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* extensions, checksum */
	0x00, 0x00
};

#undef EDID_NAME
#undef VFREQ
#undef CLOCK
#undef HACTIVE
#undef HBLANK
#undef VACTIVE
#undef VBLANK
#undef HOFFSET
#undef HPULSE
#undef VOFFSET
#undef VPULSE
#undef HSIZE
#undef VSIZE
#undef GAMMA
#undef MANUFACTURER_ID
#undef ab
#undef op
