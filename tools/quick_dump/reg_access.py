#!/usr/bin/env python3
import chipset

def read(reg):
	reg = int(reg, 16)
	val = chipset.intel_register_read(reg)
	return val

def write(reg, val):
	chipset.intel_register_write(reg, val)

def init():
	pci_dev = chipset.intel_get_pci_device()
	ret = chipset.intel_register_access_init(pci_dev, 0)
	if ret != 0:
		print("Register access init failed");
		return False
	return True

if __name__ == "__main__":
	import sys

	if init() == False:
		sys.exit()

	reg = sys.argv[1]
	print(hex(read(reg)))
	chipset.intel_register_access_fini()
