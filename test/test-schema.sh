#!/bin/bash

set -x

PWD=$(realpath `dirname $0`/../../JSON-Schema-Test-Suite/tests/draft7)

# OK
#TEST=minLength.json
#TEST=multipleOf.json
#TEST=maxProperties.json
#TEST=enum.json
#TEST=pattern.json
#TEST=exclusiveMinimum.json
#TEST=exclusiveMaximum.json
#TEST=minProperties.json
#TEST=maxItems.json
#TEST=minItems.json
#TEST=const.json
#TEST=uniqueItems.json
#TEST=maxLength.json
#TEST=maximum.json
#TEST=minimum.json
#TEST=type.json
#TEST=required.json

# boolean schema
#TEST=properties.json
#TEST=additionalItems.json
#TEST=additionalProperties.json
#TEST=patternProperties.json
#TEST=items.json
#TEST=contains.json
#TEST=propertyNames.json

# KO

#TEST=boolean_schema.json
#TEST=not.json
#TEST=anyOf.json
#TEST=refRemote.json
#TEST=if-then-else.json
#TEST=dependencies.json
#TEST=allOf.json
#TEST=oneOf.json
#TEST=default.json
#TEST=ref.json
#TEST=definitions.json
#TEST=optional/ecmascript-regex.json
#TEST=optional/zeroTerminatedFloats.json
#TEST=optional/format/uri-reference.json
#TEST=optional/format/ipv6.json
#TEST=optional/format/idn-email.json
#TEST=optional/format/uri-template.json
#TEST=optional/format/uri.json
#TEST=optional/format/json-pointer.json
#TEST=optional/format/date.json
#TEST=optional/format/relative-json-pointer.json
#TEST=optional/format/iri-reference.json
#TEST=optional/format/hostname.json
#TEST=optional/format/email.json
#TEST=optional/format/date-time.json
#TEST=optional/format/iri.json
#TEST=optional/format/ipv4.json
#TEST=optional/format/regex.json
#TEST=optional/format/time.json
#TEST=optional/format/idn-hostname.json
#TEST=optional/bignum.json
#TEST=optional/content.json

TESTS=$(find $PWD | grep json$ | egrep $TEST)

FAILCOUNT=0

for T in $TESTS
do
	echo $T
    ./ng/ng < $T
    FAILCOUNT=$(($FAILCOUNT + $?))
done

echo $FAILCOUNT tests failed
