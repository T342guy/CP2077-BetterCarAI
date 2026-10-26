#!/bin/bash
# classify.sh results.txt: touches by the kind of car, with speeds.
grep -o "speed [0-9.]* km/h (car [0-9]*: [^)]*" "$1" | awk '{sp=$2; line=$0; sub(/.*: /,"",line); split(line,f,","); lat=f[2]+0; u=f[3]+0; split(f[4],c," "); w=c[1]+0;
  k=(w!=0)?"crossing":(u<0)?"oncoming":(u==0)?((lat==0)?"parked in lane":"parked half in lane"):(lat==0)?"slow ahead":"other lane";
  n[k]++; s[k]+=sp; if (sp>40) f40[k]++} END {for (k in n) printf "  %-20s %3d touches, average %3.0f km/h, %2d above 40 km/h\n", k, n[k], s[k]/n[k], f40[k]}' | sort -k2 -rn
