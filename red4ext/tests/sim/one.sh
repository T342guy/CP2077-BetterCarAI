#!/bin/bash
# one.sh "label" "ENV=.. ENV=.." args...: one drive, one result line:
# label | state | other cars, contacts | average | off road, big moves | first contact
# [| digest] [| pedals] [| people, hit, closest [| first hit]] | time taken
source "$(dirname "$0")/common.sh"
label=$1; envs=$2; shift 2
out=$(env $envs "$SIMDIR/sim.sh" "$@")
st=$(echo "$out" | grep -o "state [0-9]: [a-z ]*" | head -1)
oc=$(echo "$out" | grep -o "[0-9]* other cars: [0-9]* contacts, closest centre-to-centre [0-9.]*")
av=$(echo "$out" | grep -o "average [0-9]* km/h")
offr=$(echo "$out" | grep -o "off the road: [0-9]* m, [0-9]* moves of 2 m\( or more, [0-9]* changes of mind\)\?")
first=$(echo "$out" | grep -m1 "contact with" | sed 's/^ *! //')
dig=$(echo "$out" | grep -o "digest [0-9a-f]*")
ped=$(echo "$out" | grep -o "pedals: travel [0-9.]* per min, hard brake [0-9.]* s per min, [0-9]* quick flips")
pp=$(echo "$out" | grep -o "[0-9]* people: [0-9]* hit, closest [0-9.]* m, [0-9]* hits it could have stopped for")
hitp=$(echo "$out" | grep -m1 "hit a person" | sed 's/^ *! //')
took=$(echo "$out" | grep -o "ended after [0-9]* s")
echo "$label | $st | $oc | $av | $offr | $first${dig:+ | $dig}${ped:+ | $ped}${pp:+ | $pp${hitp:+ | $hitp}} | $took"
