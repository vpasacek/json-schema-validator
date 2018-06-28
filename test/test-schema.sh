#!/bin/bash

set -x

PWD=$(realpath `dirname $0`/../../JSON-Schema-Test-Suite/tests/draft7)

TESTS=$(find $PWD | grep json$ | egrep 'enum')

FAILCOUNT=0

for T in $TESTS
do
	echo $T
    ./ng/ng < $T
    FAILCOUNT=$(($FAILCOUNT + $?))
done

echo $FAILCOUNT tests failed
