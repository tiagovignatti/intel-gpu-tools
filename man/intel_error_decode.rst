==================
intel_error_decode
==================

-------------------------------------------------------------------------------------
Decode an Intel GPU dump automatically captured by the kernel at the time of an error
-------------------------------------------------------------------------------------
.. include:: defs.rst
:Author: Intel Graphics for Linux <intel-gfx@lists.freedesktop.org>
:Date: 2016-03-01
:Version: |PACKAGE_STRING|
:Copyright: 2010,2012,2016 Intel Corporation
:Manual section: |MANUAL_SECTION|
:Manual group: |MANUAL_GROUP|

SYNOPSIS
========

**intel_error_decode** [*FILENAME*]

DESCRIPTION
===========

**intel_error_decode** is a tool that decodes the instructions and state of the
GPU at the time of an error. It requires kernel 2.6.34 or newer, and either
debugfs mounted on /sys/kernel/debug or /debug containing a current
i915_error_state or you can pass a file containing a saved error.

ARGUMENTS
=========

FILENAME
    Decodes a previously saved error.

REPORTING BUGS
==============

Report bugs to https://bugs.freedesktop.org.
