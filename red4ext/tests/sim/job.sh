#!/bin/bash
# job.sh "label<TAB>env<TAB>args": one pool job.
IFS=$'\t' read -r label envs args <<< "$1"
"$(dirname "$0")/one.sh" "$label" "$envs" $args
