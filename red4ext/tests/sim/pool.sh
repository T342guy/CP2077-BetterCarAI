#!/bin/bash
# pool.sh < jobs: each line "label<TAB>env vars<TAB>route args"; runs them
# $JOBS at a time, prints results sorted by label.
source "$(dirname "$0")/common.sh"
xargs -P "$JOBS" -d '\n' -n 1 "$SIMDIR/job.sh" | sort -V
