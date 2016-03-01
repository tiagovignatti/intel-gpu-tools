=======================
intel_upload_blit_small
=======================

---------------------------------------
Microbenchmark of Intel GPU performance
---------------------------------------
.. include:: defs.rst
:Author: Intel Graphics for Linux <intel-gfx@lists.freedesktop.org>
:Date: 2016-03-01
:Version: |PACKAGE_STRING|
:Copyright: 2009,2012,2016 Intel Corporation
:Manual section: |MANUAL_SECTION|
:Manual group: |MANUAL_GROUP|

SYNOPSIS
========

**intel_upload_blit_small**

DESCRIPTION
===========

**intel_upload_blit_small** is a microbenchmark tool for DRM performance. It
should be run with kernel modesetting enabled, and may require root privilege
for correct operation. It does not require X to be running.

Given that it is a microbenchmark, its utility is largely for regression testing
of the kernel, and not for general conclusions on graphics performance.

REPORTING BUGS
==============

Report bugs to https://bugs.freedesktop.org.
