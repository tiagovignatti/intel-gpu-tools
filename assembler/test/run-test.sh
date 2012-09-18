#!/bin/sh

DIR="$( cd -P "$( dirname "$0" )" && pwd )"

${DIR}/../src/intel-gen4asm -o TEST.out ${DIR}/TEST.g4a
if cmp TEST.out ${DIR}/TEST.expected 2> /dev/null;
then
  echo "Good";
else
  echo "Output comparison for TEST"
  diff -u ${DIR}/TEST.expected TEST.out
  exit 1;
fi
