#!/usr/bin/env python3

# Copyright © 2011 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#
# Authors:
#    Ben Widawsky <ben@bwidawsk.net>

#very limited C-like preprocessor

#limitations:
# no macro substitutions
# no multiline definitions
# divide operator is //

import sys,re

# make sure both input file and stdout are handled as utf-8 text, regardless
# of current locale (eg. LANG=C which tells python to use ascii encoding)
sys.stdout = open(sys.__stdout__.fileno(), "a", encoding="utf-8")
file = open(sys.argv[1], "r", encoding="utf-8")

lines = file.readlines()
len(lines)
out = dict()
defines = dict()

count = 0
#create a dict for our output
for line in lines:
    out[count] = line
    count = count + 1

#done is considered #define <name> <number>
def is_done(string):
    m = re.match("#define\s+(\w+?)\s+([a-fA-F0-9\-]+?)\s*$", string)
    return m

#skip macros, the real cpp will handle it
def skip(string):
    #macro
    m = re.match("#define\s+\w+\(.+", string)
    return m != None

#put contants which are done being evaluated into the dictionary
def easy_constants():
    ret = 0
    for lineno, string in out.items():
        if skip(string):
            continue
        m = is_done(string)
        if m != None:
            key = m.group(1)
            value = m.group(2)
            if not key in defines:
                    defines[key] = int(eval(value))
                    ret = 1
    return ret

#replace names with dictionary values
def simple_replace():
    ret = 0
    for lineno, string in out.items():
        if skip(string):
            continue
        for key, value in defines.items():
            if is_done(string):
                continue
            s = re.subn(key, repr(value), string)
            if s[1] > 0:
                out[lineno] = s[0]
                ret = s[1]
    return ret

#evaluate expressions to try to simplify them
def collapse_constants():
    ret = 0
    for lineno, string in out.items():
        if skip(string):
            continue
        if is_done(string):
            continue
        m = re.match("#define\s+(.+?)\s+(.+)$", string)
        if m != None:
            try:
                out[lineno] = "#define " + m.group(1) + " " + repr(eval(m.group(2)))
                ret = 1
            except NameError as ne:
                #this happens before a variable is resolved in simple_replace
                continue
            except SyntaxError:
                #this happens with something like #define foo bar, which the
                #regular cpp can handle
                continue
            except:
                raise KeyboardInterrupt
    return ret;

while True:
    ret = 0
    ret += easy_constants()
    ret += simple_replace()
    ret += collapse_constants()
    if ret == 0:
        break;

for lineno, string in out.items():
    print(string.rstrip())
