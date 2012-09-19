#!/bin/sh

#TODO: add new test cases in environment variables ${TEST_GEN4_XXX}

DIR="$( cd -P "$( dirname "$0" )" && pwd )"
ASSEMBLER="${DIR}/../src/intel-gen4asm"

# Tests that are expected to success because they contain correct code.
# $1 is the gen level, e.g., 4 or 7
# $2 is the test case name
function check_if_work()
{
    GEN_LEVEL="$1"
    TEST_CASE_NAME="$2"
    SOURCE="${TEST_CASE_NAME}.g${1}a"
    EXPECTED="${TEST_CASE_NAME}.expected"
    TEMP_OUT="temp.out"
    ${ASSEMBLER} -g ${GEN_LEVEL} ${DIR}/${SOURCE} -o ${TEMP_OUT}
    if cmp ${TEMP_OUT} ${DIR}/${EXPECTED} 2> /dev/null;
    then
        echo "[ OK ] ${TEST_CASE_NAME}";
    else
        echo "[FAIL] ${TEST_CASE_NAME}";
        diff -u ${DIR}/${EXPECTED} ${TEMP_OUT};
    fi
}

# Tests that are expected to fail because they contain wrong code.
function check_if_fail()
{
    GEN_LEVEL="$1"
    TEST_CASE_NAME="$2"
    SOURCE="${TEST_CASE_NAME}.g${1}a"
    TEMP_OUT="temp.out"
    ${ASSEMBLER} -g ${GEN_LEVEL} ${DIR}/${SOURCE} -o ${TEMP_OUT} 2>/dev/null
    if [ $? -eq 0 ];
    then
        echo "[FAIL] ${TEST_CASE_NAME}";
    else
        echo "[ OK ] ${TEST_CASE_NAME}";
    fi
}

# Tests that are expected to success because they contain correct code.
TEST_GEN4_SHOULD_WORK="\
	mov \
	frc \
	rndd \
	rndu \
	rnde \
	rnde-intsrc \
	rndz \
	lzd \
	not \
	jmpi \
	if \
	iff \
	while \
	else \
	break \
	cont \
	halt \
	wait \
	endif \
	declare \
	immediate \
	"

# Tests that are expected to fail because they contain wrong code.
TEST_GEN4_SHOULD_FAIL="\
	rnde-intsrc \
	"

for T in ${TEST_GEN4_SHOULD_WORK}
do
    check_if_work 4 ${T}
done

for T in ${TEST_GEN4_SHOULD_FAIL}
do
    check_if_fail 4 ${T}
done

