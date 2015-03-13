#!/bin/sh
#
# Copyright © 2014 Intel Corporation
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

#
# Check that command line handling works consistently across all tests
#

TESTLIST=`cat $top_builddir/tests/test-list.txt`
if [ $? -ne 0 ]; then
	echo "Error: Could not read test lists"
	exit 99
fi

for test in $TESTLIST; do

	if [ "$test" = "TESTLIST" -o "$test" = "END" ]; then
		continue
	fi

	if [ -x $top_builddir/tests/$test ]; then
		test=$top_builddir/tests/$test
	else
		# if the test is a script, it will be in $srcdir
		test=$top_srcdir/tests/$test
	fi

	echo "$test:"

	# check invalid option handling
	echo "  Checking invalid option handling..."
	./$test --invalid-option 2> /dev/null && exit 1

	# check valid options succeed
	echo "  Checking valid option handling..."
	./$test --help > /dev/null || exit 1

	# check --list-subtests works correctly
	echo "  Checking subtest enumeration..."
	LIST=`./$test --list-subtests`
	RET=$?
	if [ $RET -ne 0 -a $RET -ne 79 ]; then
		exit 1
	fi

	if [ $RET -eq 79 -a -n "$LIST" ]; then
		exit 1
	fi

	if [ $RET -eq 0 -a -z "$LIST" ]; then
		exit 1
	fi

	# check invalid subtest handling
	echo "  Checking invalid subtest handling..."
	./$test --run-subtest invalid-subtest > /dev/null 2>&1 && exit 1
done
