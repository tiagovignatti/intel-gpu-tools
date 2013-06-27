#!/bin/sh
die() {
	echo "$@"
	exit 1
}

if [ -d /debug/dri ] ; then
	debugfs_path=/debug/dri
fi

if [ -d /sys/kernel/debug/dri ] ; then
	debugfs_path=/sys/kernel/debug/dri
fi

i915_dfs_path=x
for dir in `ls $debugfs_path` ; do
	if [ -f $debugfs_path/$dir/i915_error_state ] ; then
		i915_dfs_path=$debugfs_path/$dir
		break
	fi
done

if [ $i915_dfs_path = "x" ] ; then
	die " i915 debugfs path not found."
fi

# read everything we can
if [ `cat $i915_dfs_path/clients | wc -l` -gt "2" ] ; then
	die "ERROR: other drm clients running"
fi

if [ -d /sys/class/drm ] ; then
    sysfs_path=/sys/class/drm
fi

i915_sfs_path=x
for dir in `ls $sysfs_path` ; do
    if [ -f $sysfs_path/$dir/error_state ] ; then
	i915_sfs_path=$sysfs_path/$dir
	break
    fi
done

if [ $i915_sfs_path = "x" ] ; then
    die " i915 sysfs path not found."
fi

