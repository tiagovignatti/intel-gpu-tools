#!/usr/bin/env python3

# register definition format:
# ('register name', 'register offset', 'register type')
#
# register types:
#  '' - normal register
#  'DPIO' - DPIO register
#
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

import argparse
import os
import sys
import ast
import subprocess
import chipset
import reg_access as reg

# Ignore lines which are considered comments
def ignore_line(line):
    if not line.strip():
        return True
    if len(line) > 1:
        if line[1] == '/' and line[0] == '/':
            return True
    if len(line) > 0:
        if line[0] == '#' or line[0] == ';':
            return True
    return False

def parse_file(file):
    print('{0:^10s} | {1:^28s} | {2:^10s}'. format('offset', file.name, 'value'))
    print('-' * 54)
    for line in file:
        if ignore_line(line):
            continue
        register = ast.literal_eval(line)
        if register[2] == 'DPIO':
            val = reg.dpio_read(register[1], 0)
        if register[2] == 'FLISDSI':
            val = reg.flisdsi_read(register[1])
        else:
            val = reg.read(register[1])
        intreg = int(register[1], 16)
        print('{0:#010x} | {1:<28} | {2:#010x}'.format(intreg, register[0], val))
    print('')

def walk_base_files():
    for root, dirs, files in os.walk('.'):
        for name in files:
            if name.startswith(("base_")):
                file = open(name.rstrip(), 'r')
                parse_file(file)

def autodetect_chipset():
    pci_dev = chipset.intel_get_pci_device()
    devid = chipset.pcidev_to_devid(pci_dev)
    if chipset.is_sandybridge(devid):
        return open('sandybridge', 'r')
    elif chipset.is_ivybridge(devid):
        return open('ivybridge', 'r')
    elif chipset.is_valleyview(devid):
        return open('valleyview', 'r')
    elif chipset.is_haswell(devid):
        return open('haswell', 'r')
    elif chipset.is_broadwell(devid):
        return open('broadwell', 'r')
    else:
        print("Autodetect of devid " + hex(devid) + " failed")
        return None

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Dumb register dumper.')
    parser.add_argument('-b', '--baseless',
            action='store_true', default=False,
            help='baseless mode, ignore files starting with base_')
    parser.add_argument('-f', '--file',
            type=argparse.FileType('r'), default=None)
    parser.add_argument('profile', nargs='?',
            type=argparse.FileType('r'), default=None)

    args = parser.parse_args()

    if reg.init() == False:
        print("Register initialization failed")
        sys.exit()

    # Put us where the script is
    os.chdir(os.path.dirname(sys.argv[0]))

    # specifying a file trumps all other things
    if args.file != None:
        parse_file(args.file)
        sys.exit()

    #parse anything named base_ these are assumed to apply for all gens.
    if args.baseless == False:
        walk_base_files()

    if args.profile == None:
        args.profile = autodetect_chipset()

    if args.profile == None:
        sys.exit()

    for extra in args.profile:
        extra_file = open(extra.rstrip(), 'r')
        parse_file(extra_file)
