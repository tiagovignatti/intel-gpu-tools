=========
intel_gtt
=========

---------------------------------------
Dump the contents of an Intel GPU's GTT
---------------------------------------
.. include:: defs.rst
:Author: Intel Graphics for Linux <intel-gfx@lists.freedesktop.org>
:Date: 2016-03-01
:Version: |PACKAGE_STRING|
:Copyright: 2010,2012,2016 Intel Corporation
:Manual section: |MANUAL_SECTION|
:Manual group: |MANUAL_GROUP|

SYNOPSIS
========

**intel_gtt**

DESCRIPTION
===========

**intel_gtt** is a tool to view the contents of the GTT on an Intel GPU. The GTT
is the page table that maps between GPU addresses and system memory. This tool
can be useful in debugging the Linux AGP driver initialization of the chip or in
debugging later overwriting of the GTT with garbage data.

REPORTING BUGS
==============

Report bugs to https://bugs.freedesktop.org.
