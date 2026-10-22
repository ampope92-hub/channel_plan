# Channel Plan: Near-Optimal Frequency Assignment for a Train Radio Network in C++

A rail operator is re-planning the channels on its train radio network. The agent gets a drive-test survey (`radios.csv`, `pairs.csv`, `barred.csv`, `licence.txt`, and a README that defines the rules and the penalty) and must write a C++ planner that assigns a channel to every radio. A plan is legal when every channel is on the licence and not barred for that radio, and radios on the same mast sit two channels apart because they share a combiner. The planner should minimise the total co-channel and adjacent-channel interference penalty over the surveyed pairs. Each run gets 60 seconds and must come within 0.75% of the proven optimum, on the shipped survey and on an unseen, larger one.

## What makes it hard

Minimum-interference frequency assignment is NP-hard in general, and the usual heuristics stall well short of the bar. Measured gaps above the optimum on the shipped survey:

- greedy assignment along the line: 30.3%
- steepest descent over whole-mast moves: 23.9%
- two minutes of random restarts: 18.7%
- simulated annealing over whole-mast channel sets: 3.1% after ten minutes, and 3.1-4.5% in the 45 seconds a graded run allows. Across eight fresh surveys it never got closer than 1.5%, and on the 7-channel second survey it is still 4.1% out after ten minutes.

The bar (0.75%) sits at the midpoint between the best anneal and the optimum. The key observation is structural. Pairs are only logged out to 18 km, so with masts ordered along the line only a few masts can still interact with anything ahead, which makes a dynamic programme over channel sets along the line feasible. Each mast becomes one variable whose values are its legal channel sets, and the state keeps only the channels of radios that still have a pair further along. The shipped survey then peaks at 57,600 states and solves exactly in a fraction of a second. The hidden survey (500 masts, 822 radios, 7 channels, denser spacing) peaks at 2.9 billion states with whole masts in the state, or 194 million with finished radios dropped. That is too large, so the search has to be bounded (keeping the cheapest states per step), which still reaches the optimum there.

## How it's graded

Grading runs in a separate, offline container. It keeps only the regular files directly in `/app/planner/src` and compiles them as an unprivileged user with `g++ -O2 -std=c++17`. A guard object (`tests/exec_guard.cpp`) is linked first so the program cannot start other processes. The binary runs on the shipped survey and then on a second survey staged only after the build. Each run has a hard 60-second kill, and interpreters and shell tools are closed to the build user. Ten checks:

- fixture consistency: the stored optima match those proved by `solution/certify.cpp`, a separately written exact branch-and-bound search
- the five shipped survey files are unchanged (sha256)
- per survey: the run finished inside 60 seconds; `radio,channel` header with one row per radio; the plan is legal; and the integer-recomputed penalty satisfies `total * 10000 <= optimum * 10075`

Plans are staged to root-only paths only if they are regular files, and are read with `O_NOFOLLOW`. Arms run through the verifier, each scoring 0:

- dropping the adjacent-channel penalty (78.2% and 119.6% above the optimum)
- breaking the mast rule
- ignoring barred channels
- writing the exact plan and then spinning past the time limit
- a 45-second anneal (3.1-4.5% and 7.6-8.7% above the optimum)
- pasting the shipped plan into the source

## Repository layout

- `instruction.md` - the task prompt given to the agent
- `task.toml` - metadata, limits, and the difficulty, solution and verification write-ups
- `environment/` - agent container: `Dockerfile`, `survey/` (shipped survey and README), `planner/` (Makefile and stub `src/planner.cpp`)
- `solution/` - `solve.sh`, reference `planner.cpp` (bounded DP along the line), `certify.cpp` (independent exact solver), `gen_survey.py` (survey generator)
- `tests/` - verifier `Dockerfile`, `test.sh`, `test_outputs.py`, `launch.c`, `exec_guard.cpp`, `golden/` and `hidden/` optima, `hidden/survey/`, `golden_ref/` reference results

## Running it

Requires Docker and Harbor (`uv tool install harbor` or `pip install harbor`). From the repo root:

```
harbor run -p . -a oracle
```

runs the reference solution through the verifier (should score reward 1). To try an agent:

```
harbor run -p . -a <agent> -m <model>
```

for example `-a claude-code`.

---

Task design, data generators, reference solution and verifier by Audrey Pope.
