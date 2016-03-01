================
intel_infoframes
================

-------------------------------
View and change HDMI InfoFrames
-------------------------------
.. include:: defs.rst
:Author: Intel Graphics for Linux <intel-gfx@lists.freedesktop.org>
:Date: 2016-03-01
:Version: |PACKAGE_STRING|
:Copyright: 2012,2016 Intel Corporation
:Manual section: |MANUAL_SECTION|
:Manual group: |MANUAL_GROUP|

SYNOPSIS
========

**intel_infoframes** [*OPTIONS*]

DESCRIPTION
===========

**intel_infoframes** is a tool to view and change the HDMI InfoFrames sent by
the GPU. Its main purpose is to be used as a debugging tool. In some cases
(e.g., when changing modes) the Kernel will undo the changes made by this tool.

Descriptions of the InfoFrame fields can be found on the HDMI and CEA-861
specifications.

OPTIONS
=======

-h, --help
    Display comprehensive help on the tool.

LIMITATIONS
===========

Not all HDMI monitors respect the InfoFrames sent to them. Only GEN 4 or newer
hardware is supported yet.

REPORTING BUGS
==============

Report bugs to https://bugs.freedesktop.org.

SEE ALSO
========

HDMI specification, CEA-861 specification.
