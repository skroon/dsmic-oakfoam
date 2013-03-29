#!/bin/bash

set -eu
WD="$(dirname "$0")"

TEMPOUTPUT="patterns_circ_`date +%F_%T`.tmp"
TEMPOUTPUT2="patterns_circ_2_`date +%F_%T`.tmp"
OAKFOAM="$WD/../../oakfoam --nobook --log $TEMPOUTPUT"

if ! test -x $WD/../../oakfoam; then
  echo "File $WD/../../oakfoam not found" >&2
  exit 1
fi

if (( $# < 1 )); then
  echo "Exactly one GAME.SGF required" >&2
  exit 1
fi

GAME=$1
INITGAMMAS=$2
SIZE=$3

CMDS="param undo_enable 0\nparam features_circ_list 0.1\nparam features_circ_list_size $SIZE\nloadfeaturegammas \"$INITGAMMAS\"\nloadsgf \"$GAME\""
# Use gogui-adapter to emulate loadsgf
echo -e $CMDS | gogui-adapter "$OAKFOAM"

cat $TEMPOUTPUT | grep -e '^[1-9][0-9]*:' | sed 's/ /\n/g' | grep '^[1-9][0-9]*:' >> $TEMPOUTPUT2

cat $TEMPOUTPUT2 | sort | uniq -c | sort -rn

rm -f $TEMPOUTPUT $TEMPOUTPUT2

