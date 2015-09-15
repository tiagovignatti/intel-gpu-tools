#!/usr/bin/env python
#
# Copyright 2015 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

from __future__ import print_function
import json
import sys

def filter_results(filename):
    with open(filename) as data:
        json_data = json.load(data)

    for test_name in json_data["tests"]:
        if json_data["tests"][test_name]["result"] == "incomplete":
            continue
        if json_data["tests"][test_name]["time"] < 60:
            print(test_name)


if len(sys.argv) < 2:
    print("Usage: quick-testlist.py RESULTS")
    print("Read piglit results from RESULTS and print the tests that executed"
          " in under 60 seconds, excluding any incomplete tests. The list can"
          " be used by the --test-list option of piglit.")
    sys.exit(1)

filter_results(sys.argv[1])
