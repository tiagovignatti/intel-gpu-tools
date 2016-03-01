==============
intel_stepping
==============

-------------------------------------------------
Display the stepping information for an Intel GPU
-------------------------------------------------
.. include:: defs.rst
:Author: Intel Graphics for Linux <intel-gfx@lists.freedesktop.org>
:Date: 2016-03-01
:Version: |PACKAGE_STRING|
:Copyright: 2009,2012,2016 Intel Corporation
:Manual section: |MANUAL_SECTION|
:Manual group: |MANUAL_GROUP|

SYNOPSIS
========

**intel_stepping**

DESCRIPTION
===========

**intel_stepping** is a tool to print the stepping information for an Intel GPU,
along with the PCI ID and revision used to determine it. It requires root
privilege to map the graphics device.

BUGS
====

Not all the known stepping IDs or chipsets are included, so the output on some
devices may not be as specific as possible.

REPORTING BUGS
==============

Report bugs to https://bugs.freedesktop.org.
