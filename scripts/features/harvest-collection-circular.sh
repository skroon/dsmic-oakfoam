#!/bin/bash

set -eu

TEMPOUTPUT="collection_circ_`date +%F_%T`.tmp"
TEMPCOMBINED="combined_circ_`date +%F_%T`.tmp"

SIZE=$1

echo "" > $TEMPOUTPUT

i=0
cat | while read GAME
do
  let "i=$i+1"
  echo -e "$i \t: '$GAME'" >&2
  ./harvest-circular.sh "$GAME" $SIZE >> ${TEMPOUTPUT}
done

cat ${TEMPOUTPUT} | ./harvest-combine.sh | sort -rn > ${TEMPCOMBINED}
cat $TEMPCOMBINED

rm -f $TEMPOUTPUT
rm -f $TEMPCOMBINED

