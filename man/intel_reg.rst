=========
intel_reg
=========

---------------------------------
Intel graphics register multitool
---------------------------------

:Author: Jani Nikula <jani.nikula@intel.com>
:Date: 2015-04-14
:Version: intel-gpu-tools
:Copyright: 2015 Intel Corporation
:Manual section: 1
:Manual group: General Commands Manual

SYNOPSIS
========

**intel_reg** [*option* ...] *command*

DESCRIPTION
===========

Intel graphics register multitool. Read, write, dump, and decode Intel graphics
MMIO and sideband registers, and more.

OPTIONS
=======

Some options are global, and some specific to commands.

--verbose
---------

Increase verbosity.

--quiet
-------

Decrease verbosity.

--count=N
---------

Read N registers.

--binary
--------

Output binary values.

--all
-----

Decode registers for all known platforms.

--mmio=FILE
-----------

Use MMIO bar from FILE.

--devid=DEVID
-------------

Pretend to be PCI ID DEVID. Useful with MMIO bar snapshots from other machines.

--spec=PATH
-----------

Read register spec from directory or file specified by PATH; see REGISTER SPEC
DEFINITIONS below for details.

--help
------

Show brief help.

COMMANDS
========

See REGISTER REFERENCES below on how to describe registers for the commands.

read [--count=N] REGISTER [...]
-------------------------------

Dump each specified REGISTER, or N registers starting from each REGISTER.

write REGISTER VALUE [REGISTER VALUE ...]
-----------------------------------------

Write each VALUE to corresponding REGISTER.

dump [--mmio=FILE --devid=DEVID]
--------------------------------

Dump all registers specified in the register spec.

decode REGISTER VALUE
---------------------

Decode REGISTER VALUE.

snapshot
--------

Output the MMIO bar to stdout. The output can be used for a later invocation of
dump or read with the --mmio=FILE and --devid=DEVID parameters.

list
----

List the known registers.

help
----

Display brief help.


REGISTER REFERENCES
===================

Registers are defined as [(PORTNAME|PORTNUM|MMIO-OFFSET):](REGNAME|REGADDR).

PORTNAME
--------

The register access method, most often MMIO, which is the default. The methods
supported on all platforms are "mmio", "portio-vga", and "mmio-vga".

On BYT and CHV, the sideband ports "bunit", "punit", "nc", "dpio", "gpio-nc",
"cck", "ccu", "dpio2", and "flisdsi" are also supported.

PORTNUM
-------

Port number for the sideband ports supported on BYT and CHV. Only numbers mapped
to the supported ports are allowed, arbitrary numbers are not accepted.

Numbers above 0xff are automatically interpreted as MMIO offsets, not port
numbers.

MMIO-OFFSET
-----------

Use MMIO, and add this offset to the register address.

Numbers equal to or below 0xff are automatically interpreted as port numbers,
not MMIO offsets.

REGNAME
-------

Name of the register as defined in the register spec.

If MMIO offset is not specified, it is picked up from the register
spec. However, ports are not; the port is a namespace for the register names.

REGADDR
-------

Register address. The corresponding register name need not be specified in the
register spec.

ENVIRONMENT
===========

INTEL_REG_SPEC
--------------

Path to a directory or a file containing register spec definitions.

REGISTER SPEC DEFINITIONS
=========================

A register spec associates register names with addresses. The spec is searched
for in this order:

#. Directory or file specified by the --spec option.

#. Directory or file specified by the INTEL_REG_SPEC environment variable.

#. Builtin register spec. Also used as fallback with a warning if the above are
   used but fail.

If a directory is specified using --spec option or INTEL_REG_SPEC environment
variable, the directory is scanned for a spec file in this order:

#. File named after the PCI device id. For example, "0412".

#. File named after the code name in lowercase, without punctuation. For
   example, "valleyview".

#. File named after generation. For example, "gen7" (note that this matches
   valleyview, ivybridge and haswell!).

Register Spec File Format
-------------------------

The register spec format is compatible with the quick_dump.py format, briefly
described below:

* Empty lines and lines beginning with "#", ";", or "//" are ignored.

* Lines *not* beginning with "(" are interpreted as file names, absolute or
  relative, to be included.

* Lines beginning with "(" are interpreted as register definitions.

Registers are defined as tuples ('REGNAME', 'REGADDR',
'PORTNAME|PORTNUM|MMIO-OFFSET'), as in REGISTER REFERENCES above. The port
description may also be an empty string to denote MMIO.

Examples:

* # this is a comment, below is an include

* vlv_pipe_a.txt

* ('GEN6_PMINTRMSK', '0x0000a168', '')

* ('MIPIA_PORT_CTRL', '0x61190', '0x180000')

* ('PLL1_DW0', '0x8000', 'DPIO')

BUGS
====

Reading some registers may hang the GPU or the machine.
