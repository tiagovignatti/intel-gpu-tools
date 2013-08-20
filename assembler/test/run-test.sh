#!/bin/sh

SRCDIR=${srcdir-`pwd`}
BUILDDIR=${top_builddir-`pwd`}

${BUILDDIR}/assembler/intel-gen4asm -o TEST.out $SRCDIR/TEST.g4a
if cmp TEST.out ${SRCDIR}/TEST.expected 2> /dev/null; then : ; else
  echo "Output comparison for TEST"
  diff -u ${SRCDIR}/TEST.expected TEST.out
  exit 1;
fi
