#!/bin/sh

TESTDIR=${srcdir-`pwd`}
BUILDDIR=${top_builddir-`pwd`}

${BUILDDIR}/src/gen4asm -o TEST.out $TESTDIR/TEST.g4a
if cmp TEST.out ${TESTDIR}/TEST.expected; then : ; else
  exit 1;
fi
