#!/usr/bin/env python3

#this script helps to convert internal debugger scripts given to us into our tools

import sys
import fileinput

def replace_with_dict(text, dicto):
	for key, val in dicto.items():
		text = text.replace(key, val)
	return text

for lines in fileinput.input([sys.argv[1]], inplace=True):
	lines = lines.strip()
	if lines == '': continue # strip empty lines
	replace_dict = {'dword(' : '../tools/intel_reg_read ', 'MMADDR + ' : '', '//' : '#', ')p;' : '', ')p ' : ' -c '}
	print(replace_with_dict(lines, replace_dict))
