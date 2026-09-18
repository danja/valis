# Oberheim DMX samples

Captures of a real DMX: mono, 48 kHz, 32-bit float. The machine's converters,
its companding and its output stage are already in them, which is why
`examples/dmx.ttl` plays them rather than synthesising an imitation.

## The numbered files are not duplicates

Every file here has distinct audio: no two have the same content hash. The
numbered ones are alternative sounds, which the DMX supported through swappable
sound chips, so more than one kick and snare is authentic to the machine.

What they are, from their duration and spectrum, with the named file each most
resembles:

| file | length | character | closest named |
|---|---|---|---|
| 01 | 0.17 s | bright, 92% above 2 kHz | a hat, brighter than `Hat_C` |
| 02 | 1.39 s | long, bright | a crash, longer than `Crash` |
| 03 | 0.13 s | very bright, short | a shaker or cabasa |
| 04 | 0.13 s | mid dominant, 1.1 kHz | a clap or rimshot |
| 05 | 1.09 s | long, bright | a crash or ride |
| 06 | 0.09 s | very bright, very short | a closed hat or tick |
| 07 | 0.17 s | bright, 4 kHz | a hat, close to `Hat_C` |
| 08 | 0.19 s | bright, 4 kHz | an open hat, close to `Hat_O` |
| 09 | 0.08 s | short, broadband with a low fundamental | a side stick |
| 10 | 0.71 s | 96% below 200 Hz, long | a floor tom or long kick |
| 11 | 0.11 s | almost entirely below 200 Hz | a kick; correlates 0.91 with `Kick02` |
| 12 | 0.14 s | low with a noise band | a snare, close to `Snare01` |

`11.wav` is the only one that correlates strongly with a named file, and even
that is not the same recording.

## How the kit maps to the machine

The DMX is eight voices, not eighteen sounds. Each voice is a plug-in card
carrying up to three variations of one instrument, and each card has its own
tuning trimmer and one of the machine's eight separate outputs. That is why
`examples/dmx.ttl` puts level, pan and tune on the card rather than on the
sound: two sounds on one card shared all three on the real machine.

| Card | Sounds here |
|---|---|
| BASS | `Kick01`, `Kick02` |
| SNARE | `Snare01`, `Snare02`, `Snare03` (the card's three volume levels) |
| HIHAT | `Hat_C`, `Hat_O` |
| TOM 1 | `TomHi`, `TomMid`, `TomLo` |
| TOM 2 | `TimbaleHi`, `TimbaleLo` (the timbale card, one of the swaps Oberheim sold) |
| CYMBAL | `Ride`, `Crash` |
| PERC 1 | `Tamborine`, `Rim` |
| PERC 2 | `Cabasa`, `Clap` |

The numbered files are there to swap in.
