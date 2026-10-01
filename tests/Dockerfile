# Verifier image, run in its own container: it sees only the paths task.toml declares as
# artifacts. It compiles the collected C++ with g++ and scores the plans with the Python standard
# library under pytest.

# watch_launch starts the planner under a seccomp filter. It is built statically in a throwaway
# stage so the final image carries the binary and not the C toolchain used for it.
FROM public.ecr.aws/docker/library/python:3.13-slim-bookworm@sha256:01f42367a0a94ad4bc17111776fd66e3500c1d87c15bbd6055b7371d39c124fb AS launcher
RUN apt-get update \
    && apt-get install -y --no-install-recommends gcc libc6-dev \
    && rm -rf /var/lib/apt/lists/*
COPY launch.c /tmp/launch.c
RUN gcc -O2 -static -Wall -o /watch_launch /tmp/launch.c

FROM public.ecr.aws/docker/library/python:3.13-slim-bookworm@sha256:01f42367a0a94ad4bc17111776fd66e3500c1d87c15bbd6055b7371d39c124fb

ENV LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

RUN apt-get update \
    && apt-get install -y --no-install-recommends g++ \
    && rm -rf /var/lib/apt/lists/*

# Same header removal as the agent's image, so a build behaves the same in both.
RUN find /usr/include \( -name 'stdc++.h' -o -name 'extc++.h' -o -name 'stdtr1c++.h' \) -delete
RUN test -z "$(find /usr/include -name 'stdc++.h' -o -name 'extc++.h' -o -name 'stdtr1c++.h')"

# The run policy object, compiled once by the same g++ the graded build uses and left root-owned
# and read-only. test.sh names it first on the link line.
COPY exec_guard.cpp /tmp/exec_guard.cpp
RUN g++ -O2 -std=c++17 -Wall -c -o /opt/exec_guard.o /tmp/exec_guard.cpp \
    && rm -f /tmp/exec_guard.cpp \
    && chown root:root /opt/exec_guard.o && chmod 444 /opt/exec_guard.o

# Verifier tooling, hash-pinned; nothing is installed at trial time.
COPY requirements.txt /tmp/requirements.txt
RUN pip install --no-cache-dir --require-hashes --no-deps -r /tmp/requirements.txt && rm -f /tmp/requirements.txt

COPY --from=launcher /watch_launch /usr/local/bin/watch_launch
RUN chmod 755 /usr/local/bin/watch_launch

# Agent-written code is compiled and run here, always as this user and never as root.
RUN useradd --create-home --uid 10001 --shell /bin/bash runner

# Landing directories for the declared artifacts. /app itself stays root-owned, so the build user
# cannot rename or replace any entry of it.
RUN mkdir -p /app/planner/src /app/survey /app/out \
    && chown -R runner:runner /app/planner /app/out

# The lowest penalty any legal plan reaches on the shipped survey, root-only. /tests is mounted
# at run time, so nothing kept there could be protected by a mode set here.
COPY golden /opt/golden
RUN chmod 700 /opt/golden && chmod 600 /opt/golden/*

# The same two optima as solution/certify.cpp proved them, apart from the reference, root-only.
COPY golden_ref /opt/golden_ref
RUN chmod 700 /opt/golden_ref && chmod 600 /opt/golden_ref/*

# The second survey and its lowest penalty. test.sh stages the survey for the planner only after
# the build has finished; the optimum never leaves root.
COPY hidden /opt/hidden
RUN chmod -R go-rwx /opt/hidden

COPY . /tests/

WORKDIR /app
