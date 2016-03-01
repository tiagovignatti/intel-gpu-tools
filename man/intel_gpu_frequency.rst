===================
intel_gpu_frequency
===================

--------------------------------
Manipulate Intel GPU frequencies
--------------------------------
.. include:: defs.rst
:Author: Intel Graphics for Linux <intel-gfx@lists.freedesktop.org>
:Date: 2016-03-01
:Version: |PACKAGE_STRING|
:Copyright: 2015-2016 Intel Corporation
:Manual section: |MANUAL_SECTION|
:Manual group: |MANUAL_GROUP|

SYNOPSIS
========

**intel_gpu_frequency** [*OPTIONS*]

DESCRIPTION
===========

A program to manipulate Intel GPU frequencies. Intel GPUs will automatically
throttle the frequencies based on system demands, up when needed, down when
not. This tool should only be used for debugging performance problems, or trying
to get a stable frequency while benchmarking.

Intel GPUs only accept specific frequencies. The tool may, or may not attempt to
adjust requests to the proper frequency if they aren't correct. This may lead to
non-obvious failures when setting frequency. Multiples of 50MHz is usually a
safe bet.

OPTIONS
=======

-e
    Lock frequency to the most efficient frequency.

-g, --get
    Get all the current frequency settings.

-s FREQUENCY, --set=FREQUENCY
    Lock frequency to an absolute value (MHz).

-c, --custom
    Set a min, or max frequency "min=X | max=Y".

-m, --max
    Lock frequency to max frequency.

-i, --min
    Lock frequency to min (never a good idea, DEBUG ONLY).

-d, --defaults
    Return the system to hardware defaults.

-h, --help
    Show help.

-v, --version
    Show version.

EXAMPLES
========

intel_gpu_frequency -gmin,cur
    Get the current and minimum frequency.

intel_gpu_frequency -s 400
    Lock frequency to 400Mhz.

intel_gpu_frequency -c max=750
    Set the max frequency to 750MHz

REPORTING BUGS
==============

Report bugs to https://bugs.freedesktop.org.
