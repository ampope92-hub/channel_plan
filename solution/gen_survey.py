#!/usr/bin/env python3
"""Rebuilds the two corridor surveys this task uses.

    python3 gen_survey.py shipped <dir>   the survey in /app/survey
    python3 gen_survey.py hidden <dir>    the second survey the verifier runs the planner on

Both are synthetic and come out byte for byte the same on every run. Masts are placed along the
line at random spacings, each carries one to three radios, and a pair of radios on different masts
is listed with a probability that falls with distance, up to the reach, with a co-channel penalty
that falls with the square of the distance and an adjacent-channel penalty a third to a fifth of
it. A few stretches of line bar one channel for every radio in them. Only the Python standard
library is used; random.Random draws the same sequence on every CPython release this runs on.
"""
import os
import random
import sys

# distances are in tenths of a kilometre
SHIPPED = {'seed': 20260924, 'sites': 300, 'channels': 6, 'spacing': (30, 70), 'reach': 180,
           'scale': 120, 'zones': 3}
HIDDEN = {'seed': 14, 'sites': 500, 'channels': 7, 'spacing': (22, 55), 'reach': 180,
          'scale': 90, 'zones': 4}


def build(seed, sites, channels, spacing, reach, scale, zones):
    rng = random.Random(seed)
    pos = 0
    site_km = []
    site_radios = []
    for _ in range(sites):
        pos += rng.randint(*spacing)
        site_km.append(pos)
        r = rng.random()
        site_radios.append(1 if r < 0.45 else (2 if r < 0.82 else 3))

    # stretches where the neighbouring licensee holds one channel; they never overlap, so no
    # radio has more than one channel barred and every mast keeps a legal channel set
    zone_list = []
    stretch = pos // zones
    for z in range(zones):
        a = z * stretch + rng.randint(0, stretch // 3)
        b = a + rng.randint(stretch // 4, stretch // 2)
        zone_list.append((a, b, [rng.randrange(channels)]))

    site_ids = rng.sample(range(100, 1000), sites)
    radios = []  # mast index of each radio, in line order
    for s in range(sites):
        radios.extend([s] * site_radios[s])
    radio_ids = rng.sample(range(1000, 10000), len(radios))

    pairs = []
    for i in range(len(radios)):
        for j in range(i + 1, len(radios)):
            si, sj = radios[i], radios[j]
            d = site_km[sj] - site_km[si]
            if d > reach:
                break
            if si == sj:
                continue
            f = 1 - d / reach
            if rng.random() < 0.3 + 0.7 * f:
                co = max(1, round(scale * f * f * rng.uniform(0.5, 1.5)))
                adj = co // rng.choice([3, 4, 5])
                pairs.append((radio_ids[i], radio_ids[j], co, adj))

    barred = []
    for i, s in enumerate(radios):
        for (a, b, chans) in zone_list:
            if a <= site_km[s] <= b:
                for c in chans:
                    barred.append((radio_ids[i], c))
    barred = sorted(set(barred))
    rad_rows = sorted((radio_ids[i], site_ids[s], site_km[s]) for i, s in enumerate(radios))
    return channels, rad_rows, pairs, barred


def write(d, channels, rad_rows, pairs, barred):
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, 'radios.csv'), 'w') as f:
        f.write('radio,site,km\n')
        f.writelines(f'{r},{s},{km // 10}.{km % 10}\n' for r, s, km in rad_rows)
    # the survey tool writes pairs in the order it measured them, either radio first
    rng = random.Random(len(pairs))
    pairs = list(pairs)
    rng.shuffle(pairs)
    with open(os.path.join(d, 'pairs.csv'), 'w') as f:
        f.write('radio_a,radio_b,co_channel,adjacent_channel\n')
        for a, b, co, adj in pairs:
            if rng.random() < 0.5:
                a, b = b, a
            f.write(f'{a},{b},{co},{adj}\n')
    with open(os.path.join(d, 'barred.csv'), 'w') as f:
        f.write('radio,channel\n')
        f.writelines(f'{r},{c}\n' for r, c in barred)
    with open(os.path.join(d, 'licence.txt'), 'w') as f:
        f.write(f'channels {channels}\n')


if __name__ == '__main__':
    if len(sys.argv) < 3 or sys.argv[1] not in ('shipped', 'hidden'):
        sys.exit('usage: gen_survey.py shipped|hidden <dir> [key=value ...]')
    cfg = dict(SHIPPED if sys.argv[1] == 'shipped' else HIDDEN)
    # key=value overrides (seed=7, sites=200, ...) are for re-running the calibration sweep
    for a in sys.argv[3:]:
        k, v = a.split('=')
        cfg[k] = tuple(int(x) for x in v.split(',')) if ',' in v else int(v)
    write(sys.argv[2], *build(**cfg))
