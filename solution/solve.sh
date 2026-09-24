#!/bin/bash
# Reference: put the exact planner in place of the stub, build it the way the Makefile does and
# plan the shipped survey once as a check. The verifier compiles src itself; this build is only
# here so a broken reference shows up at once.
set -euo pipefail

install -m 644 "$(dirname "$0")/planner.cpp" /app/planner/src/planner.cpp
make -C /app/planner
mkdir -p /app/out
/app/planner/planner /app/survey /app/out/plan.csv
