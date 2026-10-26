#!/bin/bash
# pedals.sh results.txt: pedal smoothness over the drives in a results file
# (one.sh lines): pedal travel per minute, seconds of hard braking per
# minute and quick flips between the pedals per drive, on average.
grep -o 'pedals: travel [0-9.]* per min, hard brake [0-9.]* s per min, [0-9]* quick flips' "$1" |
  awk '{t+=$3; h+=$8; q+=$12; n++} END {if (n) printf "pedals: travel %.1f/min, hard brake %.2f s/min, %.1f quick flips per drive", t/n, h/n, q/n}'
