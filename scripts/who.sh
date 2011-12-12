#!/bin/bash
#
# usage: sudo who.sh
#
# Requires root permissions to both query who has the device open,
# and to read the mappings of likely root-owned processes
#

for i in `lsof -t /dev/dri/card0`; do
	who=`readlink /proc/$i/exe`
	count=`grep /dev/dri/card0 /proc/$i/maps | wc -l | cut -f1 -d\ `
	echo "$who [$i]: $count"
done
