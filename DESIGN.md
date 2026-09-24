# channel_plan design note

## Axis
Search-hard planted-optimum optimisation (brief DESIGN_search_planted.md, approach section). New in
the ledger: no shipped task grades an optimisation objective against a known optimum. Category
Software / Algorithms (never shipped). Language C++ (std only), not used for an optimisation task.

## Task
Channel plan for a linear train-radio network. 300 masts, 516 radios, 6 channels, 1675 surveyed
interfering pairs (co-channel and adjacent-channel penalties), mast rule |a-b|>=2 (hard), barred
channels on three stretches (hard). Objective: sum of penalties. Graded: legal and within 0.75% of
the optimum, on the shipped survey and on a hidden survey (420 masts, 723 radios, 7 channels,
other spacing, reach, penalty scale, four barred stretches), 60 s per run.

## Where the optimum comes from (deviation from a literal plant)
The optimum is not planted by construction; it is certified. Interference never spans more than
18 km (stated in the README as a property of the survey), so an exact dynamic programme over the
masts in km order, whose state is the channel sets of the masts that still have a pair further on,
computes it: 4042 shipped (0.07 s, max 82,944 states), 2589 hidden (4.1 s, max 3.24 M states).
Cross-checked against exhaustive search on 10 small surveys (10/10 equal) and never beaten by any
of the ~50 annealing runs. This keeps the survey data realistic (no penalties bent around a chosen
answer) while the value the verifier holds is exact. The reference exploits the stated reach, not
the construction.

## Measured routes (scratch/calibrate: 8 generator seeds per config; runs of 120 s, SA 600 s)
Gap to the optimum, %:

| route | shipped config (8 seeds) | shipped survey | hidden config (8 seeds) | hidden survey |
|---|---|---|---|---|
| greedy by mast in line order | 21.3 - 30.8 | 30.3 | 32.8 - 45.4 | 33.5 |
| greedy + mast-level steepest descent | 14.2 - 24.5 | 23.9 | 25.0 - 32.1 | 25.3 |
| random restarts + descent, 120 s | 16.7 - 25.6 | 18.7 | 34.6 - 45.2 | 33.4 |
| tabu on mast moves, 120 s | 22.2 - 45.4 | - | 43.1 - 56.9 | - |
| annealing on single radios, 120 s | 23.6 - 44.3 | - | 10.5 - 22.8 | - |
| annealing on whole-mast sets, 600 s | 1.5 - 3.5 | 3.1 | 4.4 - 6.9 | 5.6 |
| same annealing, 45 s (chan_sa arm, real verifier, two runs) | - | 3.6, 4.1 | - | 5.6, 8.4 |
| no adjacent penalty in objective (exact otherwise) | 75 - 97 | 78.2 | - | 162.6 |
| beam over the line, width 100 | - | 0.17 | - | 1.08 |
| beam width 1000 / exact DP | 0 | 0 | 0 | 0 |
| radio-level dense DP | infeasible: 6^10 = 6e7 states shipped, 7^11 = 2e9 hidden |

Bar: best baseline anywhere is 1.5% (annealing, 600 s, shipped seed 1); midpoint to 0 = 0.75%.
Every baseline is outside it on every seed of both configs; exact DP is 0 on every seed.

## Arms (scripts/verify_arms.sh, real verifier)
oracle 1, nop 0, chan_no_adjacent 0 (gap test, 78%/163%), chan_mast_one_channel 0 (legality,
mast rule), chan_barred_ignored 0 (legality, barred), chan_spin_after_plan 0 (time limit only),
chan_sa 0 (gap test, 3.6-4.1% / 5.6-8.4% over two runs), chan_frozen_plan 0 (shipped passes, hidden fails).

## Risk
Pass rate is unmeasured. The pass route is recognising the line structure and building a state
space small enough for the 7-channel survey; a run that anneals, however well tuned, fails.
If models reliably reach for the dynamic programme this lands near 8/8.
