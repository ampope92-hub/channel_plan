# Line radio survey

This is the drive-test survey of the train radio network along the line, as handed to planning.
Masts stand beside the track, each carrying one to three radios, and every radio needs a channel
from the licence. Ids are asset numbers and carry no order.

## Files

`licence.txt` holds one line, `channels N`. The licence covers channels 0 to N-1, numbered in
frequency order, so channels whose numbers differ by one sit side by side in the band.

`radios.csv` has one row per radio: `radio` (radio id), `site` (id of the mast it is on) and `km`
(where that mast stands, in kilometres from the start of the line, to one decimal). Every radio on
a mast has the same `km`.

`pairs.csv` has one row for every pair of radios on different masts that the survey found can hear
each other: `radio_a`, `radio_b`, `co_channel`, `adjacent_channel`. The two penalties are whole
numbers, the dropped calls per 10,000 the survey predicts if that pair ends up on the same channel
(`co_channel`) or on channels one apart (`adjacent_channel`). Two or more channels apart they do
not bother each other and cost nothing. A pair is listed once, in either order. The survey van only
logs a pair out to 18.0 km; past that the terrain loss puts the other radio under the noise floor,
so no listed pair joins masts further apart than that. Radios on the same mast are never listed,
the combiner rule below covers them.

`barred.csv` lists `radio,channel` rows: channels that radio may not use. On some stretches of the
line the neighbouring licensee holds a channel, and every radio on those stretches has it barred.

## What a legal plan is

- every radio gets exactly one channel from 0 to N-1
- no radio is on a channel barred for it
- radios on the same mast are at least two channels apart, because they share one transmitter
  combiner and it needs a clear channel between its inputs

The penalty of a plan is the sum over every row of `pairs.csv` of `co_channel` when the pair shares
a channel, `adjacent_channel` when their channels are one apart, and nothing otherwise. Planning
signs off a plan when it is legal and its penalty is no more than 0.75% above the lowest penalty any
legal plan for the same survey can reach.
