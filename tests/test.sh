#!/bin/bash
# Verifier entrypoint, run in the verifier's own container. It compiles the collected planner
# sources as an unprivileged user, runs the binary on the shipped survey and on a second survey
# it has never seen, each under the 60-second limit, and hands the two plans to pytest. Nothing is
# fetched here; every tool is baked into tests/Dockerfile.

set -uo pipefail

mkdir -p /logs/verifier
echo 0 > /logs/verifier/reward.txt
exec 2>&1

RUN_LIMIT=60

as_runner() {
  setpriv --clear-groups --no-new-privs --reuid runner --regid runner "$@"
}

# Close the verifier's own files and the keys before any agent code exists in this container.
# If the build user can still reach them, the run is abandoned ungraded (reward 0).
chmod 700 /tests 2>/dev/null || true
chmod 700 /logs/verifier 2>/dev/null || true
if as_runner bash -c 'test -r /tests/test_outputs.py || ls /opt/golden || ls /opt/golden_ref || ls /opt/hidden' >/dev/null 2>&1; then
  echo "FATAL: /tests or the keys are reachable by the build user; this run cannot be graded."
  echo 0 > /logs/verifier/reward.txt
  exit 0
fi
echo "verifier files closed to the unprivileged build user"

# The collected entries must be real directories: chmod and chown follow a symbolic link named on
# their command line, so a link left where /app/out was expected could reopen /tests.
for p in /app/planner /app/planner/src /app/survey /app/out; do
  if [ -L "$p" ] || { [ -e "$p" ] && [ ! -d "$p" ]; }; then
    echo "FATAL: $p is not a directory (a link or a file was collected in its place); not graded."
    echo 0 > /logs/verifier/reward.txt
    exit 0
  fi
done
echo "collected paths are directories"

# The shipped survey stays where it landed, read-only; pytest checks it against its hashes.
chmod -R a-w /app/survey

# The box's interpreters and shell tools, and the libraries an interpreter can be loaded from,
# are closed to the build user while the planner runs, so the plan has to come out of the compiled
# C++. /usr/bin/env (setpriv starts each run through it) and watch_launch stay open; the dynamic
# loader stays open because every dynamically linked program needs it. Root is unaffected.
TOOLS="$( { find /usr/local/bin /usr/local/sbin /usr/bin /bin /usr/sbin /sbin -xdev -type f -perm /111 2>/dev/null;
             for l in $(find /usr/local/bin /usr/bin /bin -xdev -type l 2>/dev/null); do t="$(readlink -f "$l")"; [ -f "$t" ] && echo "$t"; done; } \
  | sort -u | grep -v -E 'ld-linux|/ld\.so$' | grep -v -x -F -e "$(readlink -f /usr/bin/env)" -e /usr/local/bin/watch_launch)"
INTERP_LIBS="$(find /usr/local/lib /usr/lib /lib -xdev -type f \
  \( -name 'libpython*.so*' -o -name 'libpypy*.so*' \) 2>/dev/null | sort -u)"
close_tools() { for f in $TOOLS $INTERP_LIBS; do chmod 750 "$f" 2>/dev/null || true; done; }
open_tools()  { for f in $TOOLS $INTERP_LIBS; do chmod 755 "$f" 2>/dev/null || true; done; }
trap open_tools EXIT

# Kill everything the build user still owns, by walking /proc as root.
sweep_runner() {
  local uid p
  uid="$(id -u runner)"
  for p in /proc/[0-9]*; do
    [ "$(stat -c %u "$p" 2>/dev/null)" = "$uid" ] && kill -KILL "${p#/proc/}" 2>/dev/null
  done
  return 0
}

# Only /app/planner/src is part of the contract. Anything else collected under /app/planner goes,
# a subdirectory of src goes, and of src only the regular files directly in it are kept, handed
# to root and made read-only, so the build user compiles them but cannot change them or run
# anything out of them. The agent's Makefile is not used: the graded build is the command
# below.
find /app/planner -mindepth 1 -depth ! -path /app/planner/src ! -path '/app/planner/src/*' \
     -delete 2>/dev/null || true
find /app/planner/src -mindepth 1 -maxdepth 1 -type d -exec rm -rf {} + 2>/dev/null || true
find /app/planner/src -mindepth 1 ! -type f -delete 2>/dev/null || true
if [ ! -f /app/planner/src/planner.cpp ] || [ -L /app/planner/src/planner.cpp ]; then
  echo "FATAL: /app/planner/src/planner.cpp is not a regular file in the collected project; not graded."
  echo 0 > /logs/verifier/reward.txt
  exit 0
fi
chown -Rh root:root /app/planner/src 2>/dev/null || true
find /app/planner/src -type d -exec chmod 555 {} + 2>/dev/null || true
find /app/planner/src -type f -exec chmod 444 {} + 2>/dev/null || true
chown runner:runner /app/planner
chmod 755 /app/planner
find /app/out -mindepth 1 -delete 2>/dev/null || true
chown runner:runner /app/out
chmod 755 /app/out
ls -A /app/planner /app/planner/src /app/survey

# The graded build, as the build user, with the run policy object named first so its pre-main
# entry runs before anything the collected sources arrange. dotglob so a .cpp whose name starts
# with a dot is compiled with the rest.
setsid \
  setpriv --clear-groups --no-new-privs --reuid runner --regid runner \
  bash -O dotglob -c 'cd /app/planner && timeout --signal=KILL 900 g++ -O2 -std=c++17 -o planner /opt/exec_guard.o src/*.cpp' &
bpid=$!
wait "$bpid"
bstatus=$?
echo "build exit status: $bstatus"
kill -TERM -- -"$bpid" 2>/dev/null || true
kill -KILL -- -"$bpid" 2>/dev/null || true
sweep_runner

# Seal the binary: root takes it and its directory, so no run can execute anything but the bytes
# this build produced. The digest is checked again before each run.
if [ "$bstatus" -eq 0 ] && [ -x /app/planner/planner ]; then
  chown -Rh root:root /app/planner 2>/dev/null || true
  find /app/planner -type d -exec chmod 755 {} + 2>/dev/null || true
  chmod 0755 /app/planner/planner
  BINSHA="$(sha256sum /app/planner/planner | cut -d' ' -f1)"
  echo "sealed binary sha256 $BINSHA"
else
  echo "the collected sources did not build into /app/planner/planner"
  rm -f /app/planner/planner
  BINSHA=""
fi

# run_once <survey-dir> <plan-path> <capture> <status-file>: one run of the sealed binary as the
# build user, from /app/planner, with the tools closed and a hard 60-second limit. The plan is
# moved to a root-only path only if it is a regular file, so a symlink planted at the plan path
# cannot make root-run pytest read something under /opt. The status file records whether the run
# finished inside the limit.
run_once() {
  local data="$1" wrote="$2" out="$3" status="$4" pid rc now t0 t1
  rm -f "$wrote" "$out"
  now="$(sha256sum /app/planner/planner 2>/dev/null | cut -d' ' -f1)"
  if [ -z "$BINSHA" ] || [ "$now" != "$BINSHA" ]; then
    echo "no sealed binary to run on $data"
    echo "not-run" > "$status"
    return
  fi
  close_tools
  t0="$(date +%s%3N)"
  setsid \
    timeout --signal=KILL "$RUN_LIMIT" \
    setpriv --clear-groups --no-new-privs --reuid runner --regid runner \
    env -C /app/planner HOME=/home/runner /usr/local/bin/watch_launch /app/planner/planner "$data" "$wrote" &
  pid=$!
  wait "$pid"
  rc=$?
  t1="$(date +%s%3N)"
  kill -TERM -- -"$pid" 2>/dev/null || true
  kill -KILL -- -"$pid" 2>/dev/null || true
  sweep_runner
  open_tools
  echo "run on $data: exit $rc after $((t1 - t0)) ms"
  # "killed" only when the run was stopped at the limit; the planner's own exit status is not graded
  if { [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ]; } && [ $((t1 - t0)) -ge $((RUN_LIMIT * 1000)) ]; then
    echo "killed" > "$status"; else echo "finished" > "$status"; fi
  chmod 600 "$status"
  if [ -L "$wrote" ] || [ ! -f "$wrote" ]; then
    echo "no regular file at $wrote after the run on $data"
    rm -f "$wrote"
  else
    mv "$wrote" "$out"
    chown root:root "$out"
    chmod 600 "$out"
  fi
}

echo "=== shipped survey ==="
run_once /app/survey /app/out/plan.csv /opt/plan_shipped.csv /opt/status_shipped.txt

# The second survey is staged only now, after the build and everything it started are gone, into
# a fresh directory the binary has never seen.
staged="$(mktemp -d /app/survey_XXXXXXXX)"
cp /opt/hidden/survey/* "$staged"/
chown -R root:root "$staged"
chmod 444 "$staged"/*
chmod 755 "$staged"
echo "=== second survey ==="
run_once "$staged" /app/out/plan_second.csv /opt/plan_hidden.csv /opt/status_hidden.txt
rm -rf "$staged"

sweep_runner

# -P keeps the working directory off sys.path; the interpreter is named by full path.
cd /tests
/usr/local/bin/python3 -P -m pytest -o cache_dir=/tmp/pytest_cache \
  --ctrf /logs/verifier/ctrf.json /tests/test_outputs.py -rA
if [ $? -eq 0 ]; then
  echo 1 > /logs/verifier/reward.txt
else
  echo 0 > /logs/verifier/reward.txt
fi
exit 0
