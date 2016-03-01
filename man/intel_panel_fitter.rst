==================
intel_panel_fitter
==================

--------------------------------
Change the panel fitter settings
--------------------------------
.. include:: defs.rst
:Author: Intel Graphics for Linux <intel-gfx@lists.freedesktop.org>
:Date: 2016-03-01
:Version: |PACKAGE_STRING|
:Copyright: 2012,2016 Intel Corporation
:Manual section: |MANUAL_SECTION|
:Manual group: |MANUAL_GROUP|

SYNOPSIS
========

**intel_panel_fitter** [*OPTIONS*]

DESCRIPTION
===========

**intel_panel_fitter** is a tool that allows you to change the panel fitter
settings, so you can change the size of the screen being displayed on your
monitor without changing the real pixel size of your desktop. The biggest use
case for this tool is to work around overscan done by TVs and some monitors in
interlaced mode.

OPTIONS
=======

-p PIPE
    Pipe to be used (A, B or C, but C is only present on Ivy Bridge and newer).

-x WIDTH
    Final screen width size in pixels (needs -p option).

-y HEIGHT
    Final screen height size in pixels (needs -p option).

-d
    Disable panel fitter (needs -p option, ignores -x and -y options).

-l
    List current state of each pipe.

-h
    Print the help message.

EXAMPLES
========

intel_panel_fitter -l
    List the current status of each pipe, so you can decide what to do.

intel_panel_fitter -p A -x 1850 -y 1040
    Change the pipe A size to 1850x1040 pixels.

intel_panel_fitter -p A -d
    Disable the panel fitter for pipe A.

NOTES
=====

In the future, there will be support for this feature inside the Linux Kernel.

LIMITATIONS
===========

Machines older than Ironlake are still not supported, but support may be
possible to implement.

REPORTING BUGS
==============

Report bugs to https://bugs.freedesktop.org.
