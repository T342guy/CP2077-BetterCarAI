#!/bin/bash
# score.sh [label]: dense sweeps over five obstacle kinds (parked, parked with
# BCAI_FAST, slow lead, parked 1.5 m left and right), summed. EXTRA: more env.
source "$(dirname "$0")/common.sh"
# The scenes are laid out along the routes the 0.27 planner took (its
# crossings, its multi-lane road): keep those.
export BCAI_ROUTES=${BCAI_ROUTES-0.27}
tot=0; cl=0; hit=0; st=0
for e in "X=1" "BCAI_FAST=1" "BCAI_LEAD=5" "BCAI_OBST_L=1.5" "BCAI_OBST_L=-1.5"; do
  r=$(SUMMARY=1 ENVS="$e $EXTRA" "$SIMDIR/sweep.sh")
  c=$(echo "$r" | grep -o '[0-9]*/[0-9]* clean' | cut -d/ -f1); n=$(echo "$r" | grep -o '/[0-9]* clean' | tr -dc 0-9)
  h=$(echo "$r" | grep -o '[0-9]* with contact' | cut -d' ' -f1); s=$(echo "$r" | grep -o '[0-9]* not arrived' | cut -d' ' -f1)
  tot=$((tot+n)); cl=$((cl+c)); hit=$((hit+h)); st=$((st+s))
done
echo "${1:-run}: $cl/$tot clean, $hit contact, $st not arrived"
