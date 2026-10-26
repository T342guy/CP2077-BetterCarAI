#!/bin/bash
# scene.sh city|hwy SEED LABEL [ENV=..]: one scene of a random traffic set
# (traffic_score.sh / hwy_score.sh with that SEED), run again on its own with
# any extra env (BCAI_TELE, BCAI_DBG, BCAI_OTHERS_AT...); prints the
# simulator's whole output. SIMBIN picks the build.
source "$(dirname "$0")/common.sh"
export ROUTE_D1 ROUTE_D2 ROUTE_H1 ROUTE_H2 ROUTE_H3
kind=$1; seed=$2; label=$3; shift 3
gen=$([[ $kind == hwy ]] && echo hwytraffic.py || echo randtraffic.py)
n=$([[ $kind == hwy ]] && echo 200 || echo 400)
line=$(python3 -I "$SIMDIR/$gen" $n $seed | grep "^$label ")
IFS=$'\t' read -r _ envs args <<< "$line"
env $envs "$@" "$SIMDIR/sim.sh" $args
