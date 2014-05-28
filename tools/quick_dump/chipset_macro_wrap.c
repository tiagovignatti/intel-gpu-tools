#include <stdbool.h>
#include <stdlib.h>
#include <pciaccess.h>
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

int is_cherryview(unsigned short pciid)
{
	return IS_CHERRYVIEW(pciid);
}

int is_haswell(unsigned short pciid)
{
	return IS_HASWELL(pciid);
}

int is_broadwell(unsigned short pciid)
{
	return IS_BROADWELL(pciid);
}

/* Simple helper because I couldn't make this work in the script */
unsigned short pcidev_to_devid(struct pci_device *pdev)
{
	return pdev->device_id;
}

bool igt_check_boolean_env_var(const char *env_var, bool default_value)
{
	char *val;

	val = getenv(env_var);
	if (!val)
		return default_value;

	return atoi(val) != 0;
}

