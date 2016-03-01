=============
intel_gpu_top
=============

---------------------------------------------
Display a top-like summary of Intel GPU usage
---------------------------------------------
.. include:: defs.rst
:Author: Intel Graphics for Linux <intel-gfx@lists.freedesktop.org>
:Date: 2016-03-01
:Version: |PACKAGE_STRING|
:Copyright: 2009,2011,2012,2016 Intel Corporation
:Manual section: |MANUAL_SECTION|
:Manual group: |MANUAL_GROUP|

SYNOPSIS
========

**intel_gpu_top** [*OPTIONS*]

DESCRIPTION
===========

**intel_gpu_top** is a tool to display usage information of an Intel GPU. It
requires root privilege to map the graphics device.

OPTIONS
=======

-s SAMPLES
    Number of samples to acquire per second.

-o FILE
    Collect usage statistics to FILE. If file is "-", run non-interactively
    and output statistics to stdout.

-e COMMAND
    Execute COMMAND to profile, and leave when it is finished. Note that the
    entire command with all parameters should be included as one parameter.

-h
    Show usage notes.

EXAMPLES
========

intel_gpu_top -o "cairo-trace-gvim.log" -s 100 -e "cairo-perf-trace /tmp/gvim"
    Run cairo-perf-trace with /tmp/gvim trace, non-interactively, saving the
    statistics into cairo-trace-gvim.log file, and collecting 100 samples per
    second.

Note that idle units are not displayed, so an entirely idle GPU will only
display the ring status and header.

BUGS
====

Some GPUs report some units as busy when they aren't, such that even when idle
and not hung, it will show up as 100% busy.

REPORTING BUGS
==============

Report bugs to https://bugs.freedesktop.org.
