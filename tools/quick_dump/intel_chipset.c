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

/* Simple helper because I couldn't make this work in the script */
unsigned short pcidev_to_devid(struct pci_device *pdev)
{
	return pdev->device_id;
}
