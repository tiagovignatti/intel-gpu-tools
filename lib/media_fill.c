#include "i830_reg.h"
#include "media_fill.h"

media_fillfunc_t get_media_fillfunc(int devid)
{
	media_fillfunc_t fill = NULL;

	if (IS_GEN8(devid))
		fill = gen8_media_fillfunc;
	else if (IS_GEN7(devid))
		fill = gen7_media_fillfunc;

	return fill;
}
