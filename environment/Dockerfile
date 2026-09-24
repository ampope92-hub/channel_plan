# The planner is C++ against the standard library, so the toolchain is g++ and make and nothing
# else. The Python base is the harness's own; nothing here uses its interpreter.
FROM public.ecr.aws/docker/library/python:3.13-slim-bookworm@sha256:01f42367a0a94ad4bc17111776fd66e3500c1d87c15bbd6055b7371d39c124fb

ENV LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        g++ \
        make \
        tmux \
        asciinema \
    && rm -rf /var/lib/apt/lists/*

# <bits/stdc++.h> is a GCC convenience header rather than part of the standard library, and the
# grading image does not carry it, so it is taken out here too and a build behaves the same in
# both places.
RUN find /usr/include \( -name 'stdc++.h' -o -name 'extc++.h' -o -name 'stdtr1c++.h' \) -delete
RUN test -z "$(find /usr/include -name 'stdc++.h' -o -name 'extc++.h' -o -name 'stdtr1c++.h')"

# the planner project with its stub, and the drive-test survey of the line
COPY planner /app/planner
COPY survey /app/survey
RUN mkdir -p /app/out

WORKDIR /app
