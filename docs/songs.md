# Songs as data

A song made from Valis circuits has two halves. The **score** is code: it decides which notes each part plays, and a
script writes one MIDI file per part. The **layout and the mix** are data: the tempo, the sections and their lengths,
which circuit plays each part, and how loud, where in the stereo field and how filtered each stem sits in the mix. The
second half lives in a Turtle file, `song.ttl`, so it can be read, checked and changed without touching the score.

The file is read by three things: the C++ reader (`src/model/Song.cpp`), the MCP tool `read_song`, and the Python module
`scripts/song/songlib.py`, which renders and mixes. Python reads it with [rdflib](https://rdflib.readthedocs.io/).

## The file

```turtle
@prefix val: <http://purl.org/stuff/valis/> .
@prefix :    <urn:example:my-song#> .

:song a val:Song ;
    val:title "My Song" ;
    val:tempo 100 ;                 # beats per minute
    val:loudness -14 ;              # integrated LUFS the mix is aimed at (optional)
    val:hasSection :intro , :verse ;
    val:hasPart :bass , :kit ;
    val:hasReturn :bass-verb .

:intro a val:Section ; val:index 1 ; val:bars 8  ; val:level -4 .
:verse a val:Section ; val:index 2 ; val:bars 16 ; val:level  0 .

:bass a val:Part ; val:circuit "circuits/bass.ttl" ; val:level -20 ; val:pan 0 ; val:highpass 30 .
:bass-verb a val:Return ; val:source :bass ; val:circuit "circuits/reverb-return.ttl" ; val:level -31 ; val:pan 0.3 .
```

| Term | On | Meaning | Default |
| --- | --- | --- | --- |
| `val:title` | Song | Name of the piece. Its lower-case words joined by hyphens name the mix file | none |
| `val:tempo` | Song | Beats per minute | required |
| `val:beatsPerBar` | Song | Beats in a bar | 4 |
| `val:loudness` | Song | Target integrated loudness in LUFS ([ITU-R BS.1770](https://www.itu.int/rec/R-REC-BS.1770)) | none |
| `val:tailSeconds` | Song | Seconds rendered after the last bar, so reverb and release can ring out | 10 |
| `val:index` | Section, Part, Return | Position in order. Sections must be numbered 1, 2, 3 and so on | order of the name |
| `val:bars` | Section | Length in bars | required |
| `val:level` | Section | Gain in dB of the whole mix while the section plays | 0 |
| `val:level` | Part, Return | Level in dB of the stem's playing RMS (RMS over the tenth-second windows that are not silent) | -20 |
| `val:circuit` | Part, Return | Circuit file, relative to the song file | required |
| `val:midi` | Part | MIDI file, relative to the song file | `midi/<name>.mid` |
| `val:source` | Return | The part whose stem is the return's input | required |
| `val:pan` | Part, Return | -1 left, 0 centre, 1 right. A stereo stem keeps its image and takes the pan as a balance | 0 |
| `val:highpass` | Part, Return | Second-order high-pass corner in Hz, 0 for none | 0 |

The stem's name is the local name of its IRI (`:bass` is `bass`), and its audio is `wavs/<name>.wav`. A return is a
stem too: the source stem is played through the return's circuit, then mixed like any other.

Sections are numbered explicitly with `val:index` rather than written as an `rdf:List`, so the order survives merging
and editing files in any triple order.

## Checking a file

The `read_song` tool takes a path, reads the file, and returns the song as JSON together with a list of problems. It
reports a section numbered out of order, a missing circuit or MIDI file, a return whose source is not a part, and a
value outside its range, each with the name of the thing at fault.

## Rendering and mixing

```python
import songlib
song = songlib.load("artifacts/my-song/song.ttl")
songlib.render(song)                                 # parts, then returns, into wavs/
songlib.mix(song, version="v1", arc=True)            # mixes/my-song-v1.wav at the song's loudness
```

`render` picks a `valis_render` that is newer than the source (set `VALIS_RENDER` to name one). `mix` levels each stem,
high-passes and pans it, sums them, applies the section levels if `arc` is set, fades the end, adds a synthetic room,
soft-clips, and then aims at the loudness the song asks for. It prints the loudness and the playing level of each
section, so an arrangement can be checked by number. The mix is a reference. It is measured, not heard.

A track script keeps only its score and calls these functions: `scripts/interstate-rain/track.py` is the example, and
its mix reproduces the earlier hand-written one sample for sample.

## What stays in code

Generators stay code: a walking bass line, a rain of random thunder hits, a solo built from a seeded motif. Data is
the right shape for what is a table. Fixed patterns (a drum bar, a chord progression) are the next candidates to move
into Turtle; see `TODO.md`.
