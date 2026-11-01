# Recorded sounds

Recordings of real instruments, kept as source material for sampling and for testing
`fit_modes` (`docs/shot.md`) on something other than synthetic sounds. A file name ends with its
[Freesound](https://freesound.org) sound number, so the page it came from is
`https://freesound.org/s/NUMBER/`.

All are released under [Creative Commons 0](https://creativecommons.org/publicdomain/zero/1.0/),
which allows copying, changing and using them for any purpose without asking or giving credit. The
licence was read from each sound's own page on 31 October 2026. Credit is given below anyway.

These are the lossy previews that Freesound serves without a login: MP3 at 128 kbps, not the
original files. For anything that depends on the top of the spectrum or on fine detail, download
the original from the sound's page and keep the preview only as a placeholder.

| File | Sound | By | Rate and channels | Length |
|---|---|---|---|---|
| `conga-d-sharp-4-373411.mp3` | A conga hit, D#4 | sgossner | 44.1 kHz, stereo | 1.0 s |
| `conga-tropical-678748.mp3` | A conga hit, "Kygo Tropical Conga Percussion Hit" | Cloud-10 | 44.1 kHz, stereo | 0.8 s |
| `cowbell-lp-319149.mp3` | A Latin Percussion cowbell | Mozaic | 44.1 kHz, stereo | 1.5 s |
| `cowbell-spurc-376948.mp3` | A cowbell with partials at odd multiples of 1.5 kHz, so probably synthesised | Mystikuum | 48 kHz, stereo | 1.5 s |
| `bell-536731.mp3` | A bell | egomassive | 44.1 kHz, mono | 4.6 s |

Each has a `.flac` beside it, decoded from the MP3 with ffmpeg, which is what the examples load: JUCE's MP3 decoder is off in this build (it is switched on by `JUCE_USE_MP3AUDIOFORMAT`, which JUCE ships disabled with a warning about third-party patents and code), so an MP3 does not load. The `.flac` files are lossless copies of the lossy previews, not better recordings.

Measured, not heard: the conga named D#4 has a fundamental of 82.4 Hz (E2, MIDI note 40), not D#4. The name is the drum's own key, not the pitch.

`val:Sampler` (`examples/sampler-percussion.ttl`) plays these from MIDI notes. A trial
of `fit_modes` on them is recorded in `TODO.md`.

## Oberheim DMX

`oberheim-dmx/` holds captures of the Oberheim DMX drum machine, used by `examples/dmx.ttl` and
`examples/blue-monday-sketch.ttl`. They moved here from `samples/Oberheim DMX/`.
