#!/bin/sh

IGT_EXIT_TIMEOUT=78
IGT_EXIT_SKIP=77
IGT_EXIT_SUCCESS=0
IGT_EXIT_INVALID=79
IGT_EXIT_FAILURE=99

# hacked-up long option parsing
for arg in $@ ; do
	case $arg in
		--list-subtests)
			exit $IGT_EXIT_INVALID
			;;
		--run-subtest)
			exit $IGT_EXIT_INVALID
			;;
		--debug)
			IGT_LOG_LEVEL=debug
			;;
		--help-description)
			echo $IGT_TEST_DESCRIPTION
			exit $IGT_EXIT_SUCCESS
			;;
		--help)
			echo "Usage: `basename $0` [OPTIONS]"
			echo "  --list-subtests"
			echo "  --run-subtest <pattern>"
			echo "  --debug"
			echo "  --help-description"
			echo "  --help"
			exit $IGT_EXIT_SUCCESS
			;;
	esac
done

skip() {
	echo "$@"
	exit $IGT_EXIT_SKIP
}

die() {
	echo "$@"
	exit $IGT_EXIT_FAILURE
}

do_or_die() {
	$@ > /dev/null 2>&1 || (echo "FAIL: $@ ($?)" && exit $IGT_EXIT_FAILURE)
}

if [ -d /debug/dri ] ; then
	debugfs_path=/debug/dri
fi

if [ -d /sys/kernel/debug/dri ] ; then
	debugfs_path=/sys/kernel/debug/dri
fi

i915_dfs_path=x
for minor in `seq 0 16`; do
	if [ -f $debugfs_path/$minor/i915_error_state ] ; then
		i915_dfs_path=$debugfs_path/$minor
		break
	fi
done

if [ $i915_dfs_path = "x" ] ; then
	skip " i915 debugfs path not found."
fi

# read everything we can
if [ `cat $i915_dfs_path/clients | wc -l` -gt "2" ] ; then
	[ -n "$DRM_LIB_ALLOW_NO_MASTER" ] || \
		die "ERROR: other drm clients running"
fi

whoami | grep -q root || ( echo ERROR: not running as root; exit $IGT_EXIT_FAILURE )

i915_sfs_path=
if [ -d /sys/class/drm ] ; then
    sysfs_path=/sys/class/drm
    if [ -f $sysfs_path/card$minor/error ] ; then
	    i915_sfs_path="$sysfs_path/card$minor"
    fi
fi
# sysfs may not exist as the 'error' is a new interface in 3.11

function drmtest_skip_on_simulation()
{
	[ -n "$INTEL_SIMULATION" ] && exit $IGT_EXIT_SKIP
}

drmtest_skip_on_simulation
