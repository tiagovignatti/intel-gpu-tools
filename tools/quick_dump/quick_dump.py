#!/usr/bin/env python3

# register definition format:
# ('register name', 'register offset', 'register type')
#
# register types:
#  '' - normal register
#  'DPIO' - DPIO register

import argparse
import os
import sys
import ast
import subprocess
import chipset
import reg_access as reg

def parse_file(file):
	print('{0:^10s} | {1:^28s} | {2:^10s}'. format('offset', file.name, 'value'))
	print('-' * 54)
	for line in file:
		register = ast.literal_eval(line)
		if register[2] == 'DPIO':
			val = reg.dpio_read(register[1])
		else:
			val = reg.read(register[1])
		intreg = int(register[1], 16)
		print('{0:#010x} | {1:<28} | {2:#010x}'.format(intreg, register[0], val))
	print('')


parser = argparse.ArgumentParser(description='Dumb register dumper.')
parser.add_argument('-b', '--baseless', action='store_true', default=False, help='baseless mode, ignore files starting with base_')
parser.add_argument('-a', '--autodetect', action='store_true', default=False, help='autodetect chipset')
parser.add_argument('profile', nargs='?', type=argparse.FileType('r'), default=None)
args = parser.parse_args()

if reg.init() == False:
	print("Register initialization failed")
	sys.exit()

# Put us where the script is
os.chdir(os.path.dirname(sys.argv[0]))

#parse anything named base_ these are assumed to apply for all gens.
if args.baseless == False:
	for root, dirs, files in os.walk('.'):
		for name in files:
			if name.startswith(("base_")):
				file = open(name.rstrip(), 'r')
				parse_file(file)

if args.autodetect:
	pci_dev = chipset.intel_get_pci_device()
	devid = chipset.pcidev_to_devid(pci_dev)
	if chipset.is_sandybridge(devid):
		args.profile = open('sandybridge', 'r')
	elif chipset.is_ivybridge(devid):
		args.profile = open('ivybridge', 'r')
	elif chipset.is_valleyview(devid):
		args.profile = open('valleyview', 'r')
	elif chipset.is_haswell(devid):
		args.profile = open('haswell', 'r')
	elif chipset.is_broadwell(devid):
		args.profile = open('broadwell', 'r')
	else:
		print("Autodetect of devid " + hex(devid) + " failed")

if args.profile == None:
	sys.exit()

for extra in args.profile:
	extra_file = open(extra.rstrip(), 'r')
	parse_file(extra_file)
