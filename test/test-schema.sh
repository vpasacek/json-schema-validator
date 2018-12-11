#!/bin/bash

set -x

PWD=$(realpath `dirname $0`/../../JSON-Schema-Test-Suite/tests/draft7)

#minLength.json
#multipleOf.json
#maxProperties.json
#enum.json
#pattern.json
#exclusiveMinimum.json
#exclusiveMaximum.json
#minProperties.json
#maxItems.json
#minItems.json
#const.json
#uniqueItems.json
#maxLength.json
#maximum.json
#minimum.json
#type.json
#required.json
#properties.json
#additionalItems.json
#additionalProperties.json
#patternProperties.json
#items.json
#contains.json
#propertyNames.json
#boolean_schema.json
#
#not.json
#anyOf.json
#allOf.json
#oneOf.json
#dependencies.json
#if-then-else.json
#default.json

TESTS="
definitions.json

refRemote.json
ref.json

optional/ecmascript-regex.json
optional/zeroTerminatedFloats.json
optional/format/uri-reference.json
optional/format/ipv6.json
optional/format/idn-email.json
optional/format/uri-template.json
optional/format/uri.json
optional/format/json-pointer.json
optional/format/date.json
optional/format/relative-json-pointer.json
optional/format/iri-reference.json
optional/format/hostname.json
optional/format/email.json
optional/format/date-time.json
optional/format/iri.json
optional/format/ipv4.json
optional/format/regex.json
optional/format/time.json
optional/format/idn-hostname.json
optional/bignum.json
optional/content.json
"


FAILCOUNT=0

for T in $TESTS
do
	T2=$(find $PWD | grep json$ | egrep $T)
	echo $T2
    ./ng/ng < $T2

    FAILCOUNT=$(($FAILCOUNT + $?))
	if [ $FAILCOUNT -ne 0 ]
	then
		exit 1
	fi

done

echo $FAILCOUNT tests failed
