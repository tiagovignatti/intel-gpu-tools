#!/usr/bin/env python3
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4
import chipset

def read(reg):
    val = chipset.intel_register_read(reg)
    return val

def write(reg, val):
    chipset.intel_register_write(reg, val)

def gen6_forcewake_get():
    write(0xa18c, 0x1)
    read(0xa180)

def mt_forcewake_get():
    write(0xa188, 0x10001)
    read(0xa180)

def vlv_forcewake_get():
    write(0x1300b0, 0x10001)
    read(0x1300b4)

# don't be clever, just try all possibilities
def get_wake():
    gen6_forcewake_get()
    mt_forcewake_get()
    vlv_forcewake_get()

def dpio_read(reg, phy):
    phy = int(phy)

    val = chipset.intel_dpio_reg_read(reg, phy)
    return val

def flisdsi_read(reg):
    val = chipset.intel_flisdsi_reg_read(reg)
    return val


def init():
    pci_dev = chipset.intel_get_pci_device()
    ret = chipset.intel_register_access_init(pci_dev, 0)
    if ret != 0:
        print("Register access init failed");
        return False

    if chipset.intel_register_access_needs_fakewake():
        print("Forcing forcewake. Don't expect your system to work after this.")
        get_wake()

    return True

if __name__ == "__main__":
    import sys

    if init() == False:
        sys.exit()

    reg = sys.argv[1]
    print(hex(read(int(reg,16))))
    chipset.intel_register_access_fini()
