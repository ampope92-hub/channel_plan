"""Grades the two channel plans test.sh captured.

test.sh built the collected C++ and ran it on the shipped survey and on a second survey it had
never seen, each under a 60-second limit, and moved each plan to a root-only path. This module
reads those captures, the two surveys and the two optima under /opt; it never runs agent code.
Every check is one instruction.md states, and both surveys are held to the same checks.

The optima are the lowest penalty any legal plan reaches on each survey. They were computed by the
reference planner (solution/planner.cpp), an exact dynamic programme over the masts in line order,
whose result was checked against exhaustive search on small surveys, and are stored as integers.
The penalty of a plan is recomputed here from the survey with integer arithmetic, and the bar is
applied without rounding, in basis points: total * 10000 <= optimum * (10000 + GAP_BP).
"""
import csv
import hashlib
import os
import pathlib
import re

import pytest

GAP_BP = 75  # 0.75%, the bar instruction.md states

SHIPPED_SURVEY = pathlib.Path("/app/survey")
HIDDEN_SURVEY = pathlib.Path("/opt/hidden/survey")

RUNS = [
    pytest.param(pathlib.Path("/opt/plan_shipped.csv"), pathlib.Path("/opt/status_shipped.txt"),
                 SHIPPED_SURVEY, pathlib.Path("/opt/golden/optimum.txt"), id="shipped"),
    pytest.param(pathlib.Path("/opt/plan_hidden.csv"), pathlib.Path("/opt/status_hidden.txt"),
                 HIDDEN_SURVEY, pathlib.Path("/opt/hidden/optimum.txt"), id="hidden"),
]

# frozen when the task was built; instruction.md says to leave /app/survey as it shipped
SHIPPED_SHA = {
    "radios.csv": "ab77995122921121dad8c9647c6aef872dd6b06716692c399b5fef80a06cc461",
    "pairs.csv": "b8fe7a448751a90454340442de7ae6d5d28e8a6edbb5314371a1f682e88c8b29",
    "barred.csv": "bcb672a86781877155777227a5f4379a7694fe55dda1dd36efde5b5e37714903",
    "licence.txt": "977938088181526990f96fa3b810c91148beae6ecc26aec0a98e0c6b327c146c",
    "README.md": "934472905439ca3f7e3f17019d5a84be87705bedd522077ad15694f1098c4276",
}


def _open(path, mode="r"):
    """Open without following a final symlink."""
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    return os.fdopen(fd, mode) if "b" in mode else os.fdopen(fd, mode, newline="")


def _survey(d):
    with _open(d / "licence.txt") as fh:
        word, n = fh.read().split()
    assert word == "channels"
    channels = int(n)
    with _open(d / "radios.csv") as fh:
        site = {int(r["radio"]): int(r["site"]) for r in csv.DictReader(fh)}
    with _open(d / "pairs.csv") as fh:
        pairs = [(int(r["radio_a"]), int(r["radio_b"]), int(r["co_channel"]),
                  int(r["adjacent_channel"])) for r in csv.DictReader(fh)]
    with _open(d / "barred.csv") as fh:
        barred = {(int(r["radio"]), int(r["channel"])) for r in csv.DictReader(fh)}
    return channels, site, pairs, barred


def _plan(path):
    assert path.exists() and not path.is_symlink(), f"no plan was written on this run ({path.name})"
    with _open(path) as fh:
        rows = [row for row in csv.reader(fh) if row]  # a blank line is no record
    assert rows[:1] == [["radio", "channel"]], f"the plan's header must be radio,channel, got {rows[:1]!r}"
    plan = {}
    for n, row in enumerate(rows[1:], start=2):
        assert len(row) == 2 and all(re.fullmatch(r"[0-9]+", c) for c in row), f"record {n} is {row!r}"
        radio, ch = map(int, row)
        assert radio not in plan, f"radio {radio} appears more than once in the plan"
        plan[radio] = ch
    return plan


def _penalty(plan, pairs):
    total = 0
    for a, b, co, adj in pairs:
        d = abs(plan[a] - plan[b])
        total += co if d == 0 else (adj if d == 1 else 0)
    return total


def test_shipped_survey_unchanged():
    """The five files in /app/survey are byte for byte what shipped."""
    for name, digest in SHIPPED_SHA.items():
        with _open(SHIPPED_SURVEY / name, "rb") as fh:
            got = hashlib.sha256(fh.read()).hexdigest()
        assert got == digest, f"/app/survey/{name} was changed"


@pytest.mark.parametrize("plan_path,status_path,survey,optimum_path", RUNS)
def test_run_finished_inside_limit(plan_path, status_path, survey, optimum_path):
    """The planner finished inside the 60 seconds each run gets."""
    with _open(status_path) as fh:
        status = fh.read().strip()
    assert status == "finished", (
        "the planner was stopped at the 60-second limit" if status == "killed"
        else "there was no built planner to run")


@pytest.mark.parametrize("plan_path,status_path,survey,optimum_path", RUNS)
def test_plan_covers_every_radio_once(plan_path, status_path, survey, optimum_path):
    """The plan has the header and exactly one row for every radio in radios.csv, no others."""
    _, site, _, _ = _survey(survey)
    plan = _plan(plan_path)
    missing = sorted(set(site) - set(plan))
    extra = sorted(set(plan) - set(site))
    assert not missing, f"{len(missing)} radios have no channel, e.g. {missing[:5]}"
    assert not extra, f"{len(extra)} rows name radios that are not in the survey, e.g. {extra[:5]}"


@pytest.mark.parametrize("plan_path,status_path,survey,optimum_path", RUNS)
def test_plan_is_legal(plan_path, status_path, survey, optimum_path):
    """Channels are in the licence, none is barred for its radio, and radios on one mast are two apart."""
    channels, site, _, barred = _survey(survey)
    plan = _plan(plan_path)
    assert set(plan) == set(site), "the plan does not cover exactly the survey's radios"
    outside = [r for r, c in plan.items() if not 0 <= c < channels]
    assert not outside, f"{len(outside)} radios are on channels outside 0..{channels - 1}, e.g. {outside[:5]}"
    on_barred = sorted(r for r, c in plan.items() if (r, c) in barred)
    assert not on_barred, f"{len(on_barred)} radios are on a channel barred for them, e.g. {on_barred[:5]}"
    by_site = {}
    for r, s in site.items():
        by_site.setdefault(s, []).append(plan[r])
    close = sorted(s for s, chans in by_site.items()
                   for i in range(len(chans)) for j in range(i + 1, len(chans))
                   if abs(chans[i] - chans[j]) < 2)
    assert not close, f"{len(set(close))} masts have radios less than two channels apart, e.g. {close[:5]}"


@pytest.mark.parametrize("plan_path,status_path,survey,optimum_path", RUNS)
def test_penalty_within_gap_of_optimum(plan_path, status_path, survey, optimum_path):
    """The plan's total penalty is no more than 0.75% above the survey's optimum."""
    _, site, pairs, _ = _survey(survey)
    plan = _plan(plan_path)
    assert set(plan) == set(site), "the plan does not cover exactly the survey's radios"
    with _open(optimum_path) as fh:
        optimum = int(fh.read().strip())
    total = _penalty(plan, pairs)
    assert total * 10000 <= optimum * (10000 + GAP_BP), (
        f"total penalty {total} is {100.0 * (total - optimum) / optimum:.2f}% above the lowest "
        f"a legal plan reaches here; the bar is {GAP_BP / 100}%")
