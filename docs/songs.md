# Songs as data

A song made from Valis circuits has two halves. The **score** decides which notes each part plays, and ends as one MIDI
file per part. Fixed patterns and chords can be written as data (see "The score" below); generated parts are code. The **layout and the mix** are data: the tempo, the sections and their lengths,
which circuit plays each part, and how loud, where in the stereo field and how filtered each stem sits in the mix. The
second half lives in a Turtle file, `song.ttl`, so it can be read, checked and changed without touching the score.

The file is read by the C++ reader (`src/model/Song.cpp`), which the MCP tools `read_song`, `render_song` and `mix_song` and
the command line tool `valis-song` all use. A score script reads it from Python with [rdflib](https://rdflib.readthedocs.io/)
(`scripts/song/songlib.py`) for the tempo and the section lengths it needs to place notes.

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
| `val:fadeSeconds` | Song | Length of the fade at the end of the mix | 4 |
| `val:roomAmount` | Song | A synthetic room added to the mix, as a fraction of it, 0 to 1 | 0.10 |
| `val:roomDecay` | Song | Seconds the room takes to fall by 60 dB, 0.02 to 5 | 0.4 |
| `val:roomLowpass` | Song | Corner in Hz of the low-pass that darkens the room's tail, 200 to 20000 | 4500 |
| `val:drive` | Song | Gain into the final soft clip (a hyperbolic tangent): 1 is nearly clean, higher is harder | 1.3 |
| `val:index` | Section, Part, Return | Position in order. Sections must be numbered 1, 2, 3 and so on | order of the name |
| `val:bars` | Section | Length in bars | required |
| `val:level` | Section | Gain in dB of the whole mix while the section plays | 0 |
| `val:level` | Part, Return | Level in dB of the stem's playing RMS (RMS over the tenth-second windows that are not silent) | -20 |
| `val:circuit` | Part, Return | Circuit file, relative to the song file | required |
| `val:midi` | Part | MIDI file, relative to the song file | `midi/<name>.mid` |
| `val:midi` | Return | MIDI file played into the return's circuit as well as the source stem (for a Leslie speed switch, say) | none |
| `val:silent` | Part | `true` renders the part but leaves it out of the mix | false |
| `val:follows` | Part | Another part. This part takes that part's gain plus its own `val:level`, which is therefore a number of dB under the leader's gain (0 or below, since a level above 0 is refused), instead of being levelled by its own rms. For sparse percussion that should keep its balance with the kit | none |
| `val:source` | Return | The part whose stem is the return's input | required |
| `val:pan` | Part, Return | -1 left, 0 centre, 1 right. A stereo stem keeps its image and takes the pan as a balance | 0 |
| `val:highpass` | Part, Return | Second-order high-pass corner in Hz, 0 for none | 0 |

The stem's name is the local name of its IRI (`:bass` is `bass`), and its audio is `wavs/<name>.wav`. A return is a
stem too: the source stem is played through the return's circuit, then mixed like any other.

Sections are numbered explicitly with `val:index` rather than written as an `rdf:List`, so the order survives merging
and editing files in any triple order.

## The score

The notes can be data too. A song names a score file with `val:score "score.ttl"`, and `valis-song score` plays it into
one MIDI file for each part it plays. The file holds patterns written as strings of steps, chords with their voicings, a
progression, how loosely each part is played, and a plan. A part the score does not play keeps its MIDI file, so a
generator that builds a bass line or a solo can still be a script.

```turtle
:score a val:Score ; val:seed 85 ;
    val:progression "C C Db C C C Fm Db" ;              # a chord a bar, from the start of each section
    val:hasChord :C , :Db , :Fm ; val:hasPattern :onedrop , :skank ; val:hasFeel :kit-feel ;
    val:hasPlay :groove-kit , :groove-stabs .

:C  a val:Chord ; val:root 36 ; val:tones "60 64 67" .
:Db a val:Chord ; val:root 37 ; val:tones "61 65 68" .
:Fm a val:Chord ; val:root 41 ; val:tones "60 65 68" .

:onedrop a val:Pattern ; val:hasLane                      # one bar of 4/4, sixteen steps
    [ val:key 36 ; val:steps "....|....|X...|...." ] ,   # X is an accented hit, x a hit, . a rest
    [ val:key 42 ; val:steps "x.x.|x.x.|x.x.|x.x." ; val:velocity 70 ] .
:skank a val:Pattern ; val:hasLane
    [ val:voice "chord" ; val:steps "....|x...|....|x..." ; val:length 2 ; val:octave 1 ] .

:kit-feel a val:Feel ; val:part "kit" ; val:timing 4 ; val:velocity 5 ; val:swing 0.54 .
:groove-kit   a val:Play ; val:section "groove" ; val:part "kit"   ; val:pattern :onedrop .
:groove-stabs a val:Play ; val:section "groove" ; val:part "stabs" ; val:pattern :skank ; val:fromBar 5 .
```

| Term | On | Meaning | Default |
| --- | --- | --- | --- |
| `val:score` | Song | The score file, relative to the song file | none |
| `val:seed` | Score | Seed for the humanising. The same score and seed always give the same notes, on every platform | 1 |
| `val:progression` | Score, Play | Chord names separated by spaces, one for each `val:barsPerChord` bars, repeated from the start of each section | none |
| `val:barsPerChord` | Score | Bars to a chord | 1 |
| `val:root`, `val:tones`, `val:pickup` | Chord | The key a bass plays, the keys of the voicing (lowest first), and a second bass key that leads into the next chord | required, required, the root |
| `val:bars`, `val:stepsPerBeat` | Pattern | Length in bars, and steps to a beat (4 is sixteenth notes) | 1, 4 |
| `val:steps` | Lane | One character a step: `x` hit, `X` accented hit, `.` or `-` rest. Spaces and `|` are ignored. The length must fill the pattern exactly | required |
| `val:key` or `val:voice` | Lane | A MIDI key, or `chord` (every tone), `root`, `pickup`, `top` or `low` from the chord of the bar | one of them |
| `val:velocity`, `val:accent` | Lane | Velocity, and what `X` adds | 90, 20 |
| `val:length`, `val:gate` | Lane | Length in steps, and the fraction of it that is held, so the note ends before the next step | 1, 0.8 |
| `val:octave`, `val:transpose` | Lane | Moves a voice by octaves, or any lane by semitones | 0, 0 |
| `val:part`, `val:timing`, `val:velocity`, `val:swing` | Feel | The part; standard deviation of each start in milliseconds and of each velocity; where the second step of a pair falls, 0.5 to 0.75 | exact |
| `val:section`, `val:part`, `val:pattern` | Play | Which part plays which pattern in which section (names are the local names of the IRIs) | required |
| `val:fromBar`, `val:toBar`, `val:every` | Play | The bars of the section it covers, and how often the pattern restarts | 1, the end, its own length |

A bar is `val:beatsPerBar` beats of the song, so a 4/4 pattern at four steps to a beat has 16 steps in a bar. A pattern
is placed in the song by stream position, and a note that falls in a bar is counted in that bar. No note starts before
the first beat, however loose the feel. The humanising comes from a small generator defined in the code (not the
standard library's, whose results differ between platforms) seeded from `val:seed` and the part's name, so a score
gives the same MIDI on every machine. It is not the same as the sequence Python's `random` gave, so a song first written
as a script will sound slightly different when its score is written as data.

`valis-song score song.ttl` writes the MIDI files; `valis-song score song.ttl --only kit` writes one part. Then render
and check the stems as before.

## Checking a file

The `read_song` tool takes a path, reads the file, and returns the song as JSON together with a list of problems. It
reports a section numbered out of order, a missing circuit or MIDI file, a return whose source is not a part, and a
value outside its range, each with the name of the thing at fault.

## Rendering and mixing

Six operations work on a song file. Each is an op in `src/ops/`, so the MCP tools and the command line tool are the same
code:

| MCP tool | `valis-song` | What it does |
| --- | --- | --- |
| `read_song` | `valis-song check song.ttl` | Reads the file, lists the layout and any circuit or MIDI file it names that is missing |
| `render_song` | `valis-song render song.ttl [--only a,b]` | Writes `wavs/<stem>.wav` for each stem |
| `score_song` | `valis-song score song.ttl [--only a,b]` | Plays the song's score into a MIDI file for each part it plays (see below) |
| `check_stems` | `valis-song stems song.ttl` | Compares each stem with its MIDI file bar by bar and reports silence where there are notes (a note counts as heard if the stem sounds from its onset to 100 ms past the end of its bar), clipping and non-finite samples |
| `reaper_project` | `valis-song reaper song.ttl [--output f.rpp] [--overwrite]` | Writes `<title>.rpp` beside the song: a track per stem with the mix's volume (the loudest at 0 dB) and pan, the tempo and the meter. The high-pass is not written, and an existing project is never replaced. With `--midi` each track holds a Valis instrument already loaded with the part's circuit and its MCP server switched on, at port 7677 for the first track and up, so the project opens ready to play and each instance can be found by its track name at `/health` |
| `mix_song` | `valis-song mix song.ttl --version v1 [--arc] [--lufs N]` | Writes `mixes/<title>-<version>.wav` and reports the loudness and each section's level |

**Render.** A part is its circuit played by its MIDI file at the song's tempo with the transport rolling, for the length of
the song plus its tail. A return is its source part's wav played through the return's circuit. Each stem is a private copy of
the circuit (a model, a compiler and an engine of its own), so the live circuit in a running Valis is not touched and stems
render several at a time. A stem that will not load, or that holds a NaN, is named in the failure and not written; the others
still are. `--only` redoes the named parts and their returns.

**Mix.** Each stem is high-passed, levelled so its playing RMS is its `val:level`, and panned with a constant-power law (a
stereo stem keeps its image and takes the pan as a balance). The stems are summed, then the sections' levels are applied if
`--arc` is given (stepped, then smoothed over about two seconds), then the fade, the room, the soft clip, and a gain that sets the
peak to -1 dBFS. If the song has a loudness, or `--lufs` gives one, a last gain brings the mix to that
[integrated loudness](https://tech.ebu.ch/docs/r/r128.pdf), held back so the true peak stays at or under -1 dBTP. The reply
says when the ceiling, not the target, set the gain. The room is noise shaped by a fixed seed, so a mix is repeatable.

A mix is never replaced: asking for a version that exists fails unless `--overwrite` is given, so every version is kept. The mix is
a reference to judge the arrangement by. It is measured, not heard, and vocals and mastering are done in a DAW.

**Followers and played returns.** A sparse part such as percussion is levelled by its own playing RMS, which would make a
few hits as loud as a full kit. Give it `val:follows` and the name of another part: it then takes that part's gain, adjusted by
its own `val:level` as an offset in dB, so it keeps the balance it was played at. Only a part that is not itself a
follower can be followed, and a return cannot follow. A return may also name a `val:midi` file, which is played into its
circuit as well as the source stem; a rotating speaker whose speed is a MIDI note is the example.
A part that exists only to feed a return takes `val:silent true`: it is rendered, so the return can read its stem, but it is
left out of the mix. A return cannot be silent, and a silent part cannot be followed.

**The room.** `val:roomAmount` is how much of it is added, `val:roomDecay` how long it takes to fall by 60 dB, and
`val:roomLowpass` darkens its tail. The impulse is four decays long, noise from a fixed seed, so the mix is repeatable.

A track script keeps only its score and calls these through `songlib.render(song)` and `songlib.mix(song, version, arc=True)`.
`scripts/interstate-rain/track.py`, `scripts/dub-metal/track.py`, `scripts/lights-on-nobody-home/track.py` (a mariachi
band, no arc) and `scripts/autumn-leaves/track.py` (a follower and a played return) are the examples. The two
older scripts that mixed in Python, `electro83` and `halloween`, are kept unconverted in `scripts/archive/`. The C++ mix matches the Python one it
replaced to within -74 dB of the signal when the room is switched off; the room differs only in its noise. Converting `lights-on-nobody-home` and `autumn-leaves` gave mixes within 0.2 LU of the
Python ones (measured, not heard); the C++ mix keeps stereo stems stereo, where the scripts folded them to mono.

## What stays in code

Generators stay code: a walking bass line, a rain of random thunder hits, a solo built from a seeded motif. Data is
the right shape for what is a table, and a drum bar, a chord progression and a plan of which part plays when are tables,
so they are in the score. A script and a score can share a song: the score writes the parts it plays and the script writes
the rest.
