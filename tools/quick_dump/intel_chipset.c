#include "intel_chipset.h"

int is_sandybridge(unsigned short pciid)
{
	return IS_GEN6(pciid);
}

int is_ivybridge(unsigned short pciid)
{
	return IS_IVYBRIDGE(pciid);
}

int is_valleyview(unsigned short pciid)
{
	return IS_VALLEYVIEW(pciid);
}
