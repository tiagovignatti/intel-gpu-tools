/*
 * Copyright © 2014 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

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

int is_skylake(unsigned short pciid)
{
	return IS_SKYLAKE(pciid);
}

/* Simple helper because I couldn't make this work in the script */
unsigned short pcidev_to_devid(struct pci_device *pdev)
{
	return pdev->device_id;
}
