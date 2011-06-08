#!/usr/bin/env python
#
# Usage:
#  scripts/throttle.py trace-dat
#
# Shows how often the trace throttles and for how long.

import getopt
from tracecmd import *
import sys

requests = {}
throttle = {}
prev_throttle = 0;

def read_events(t):
	for cpu in range(0, t.cpus):
		e = t.read_event(cpu)
		while e:
			if e.name == 'i915_gem_request_complete':
				seqno = e.num_field('seqno')
				requests[seqno] = e.ts;

			if e.name == 'i915_gem_request_throttle_begin':
				seqno = e.num_field('seqno')
				throttle[seqno] = e.ts

			if e.name == 'i915_gem_request_throttle_end':
				global prev_throttle

				ts = 0
				sum_dispatch = 0
				num_dispatch = 0
				max_dispatch = 0

				seqno = e.num_field('seqno')
				s = prev_throttle
				if s == 0:
					s = seqno
				while s <= seqno:
					if requests.has_key(s):
						if ts:
							delta = requests[s] - ts
							num_dispatch += 1
							sum_dispatch += delta
							if delta > max_dispatch:								max_dispatch = delta
						ts = requests[s]
					s += 1
					
				if throttle.has_key(seqno) and throttle.has_key(prev_throttle) and num_dispatch:
					print "throttle +%d: %dms -- %d dispatch, avg %.3fms, max %dus" % ((throttle[seqno]-throttle[prev_throttle])/1000000, (e.ts - throttle[seqno]) / 1000000, num_dispatch, sum_dispatch / (1000000. * num_dispatch), max_dispatch / 1000)
					throttle[seqno] = e.ts

				prev_throttle = seqno

			e = t.read_event(cpu)

if __name__ == "__main__":
    	if len(sys.argv) >=2:
       		filename = sys.argv[1]
	else:
		filename = "trace.dat"

	print "Initializing trace '%s'..." % (filename)
	trace = Trace(filename)
	read_events(trace)
	
