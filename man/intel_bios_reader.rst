=================
intel_bios_reader
=================

--------------------------------------------------
Parse an Intel BIOS and display many of its tables
--------------------------------------------------
.. include:: defs.rst
:Author: Intel Graphics for Linux <intel-gfx@lists.freedesktop.org>
:Date: 2016-03-01
:Version: |PACKAGE_STRING|
:Copyright: 2010,2012,2016 Intel Corporation
:Manual section: |MANUAL_SECTION|
:Manual group: |MANUAL_GROUP|

SYNOPSIS
========

**intel_bios_reader** *FILENAME*

DESCRIPTION
===========

**intel_bios_reader** is a tool to parse the contents of an Intel video BIOS
file. The file can come from **intel_bios_dumper(1)**. This can be used for
quick debugging of video bios table handling, which is harder when done inside
of the kernel graphics driver.

REPORTING BUGS
==============

Report bugs to https://bugs.freedesktop.org.

SEE ALSO
========

**intel_bios_dumper(1)**
