# scripts/song/songlib.py
#
# What every song script shares: reading a song file (a Turtle file with one val:Song, see docs/songs.md) for the layout
# a score needs, and rendering and mixing it. A track's own script keeps only its score.
#
#   import songlib
#   song = songlib.load("artifacts/my-song/song.ttl")
#   songlib.render(song)                       # every part, then the returns
#   songlib.mix(song, version="v1", arc=True)  # the mixdown named by the song's title, at the song's loudness
#
# The rendering and the mixing are done by valis-song (src/tools/SongMain.cpp), which calls the same operations as the MCP
# tools render_song and mix_song; this module only runs it. The same file is checked, with located reasons, by the MCP
# tool read_song and by Song::read in C++. This reader only reads: run `valis-song check` first if a file might be wrong.
import json, os, subprocess, sys

VALIS = os.environ.get("VALIS", os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")))

VAL = "http://purl.org/stuff/valis/"


def _local(iri):
    return str(iri).replace("#", "/").rsplit("/", 1)[-1]


class Section:
    def __init__(self, name, label, index, bars, level, first_bar):
        self.name, self.label, self.index, self.bars, self.level, self.first_bar = name, label, index, bars, level, first_bar


class Stem:
    def __init__(self, name, is_return, circuit, midi, source, level, pan, highpass):
        self.name, self.is_return, self.circuit, self.midi = name, is_return, circuit, midi
        self.source, self.level, self.pan, self.highpass = source, level, pan, highpass


class Song:
    def __init__(self, path):
        self.path = os.path.abspath(path)
        self.dir = os.path.dirname(self.path)
        self.title = self.name = ""
        self.tempo, self.beats_per_bar, self.loudness, self.tail = 120.0, 4, None, 10.0
        self.fade, self.room, self.drive = 4.0, 0.10, 1.3
        self.room_decay, self.room_lowpass = 0.4, 4500.0
        self.sections, self.stems = [], []

    # -- the clock, for a score that wants the length in bars
    @property
    def bars(self):
        return sum(s.bars for s in self.sections)

    @property
    def midi_dir(self):
        return os.path.join(self.dir, "midi")

    @property
    def parts(self):
        return [s for s in self.stems if not s.is_return]


def load(path):
    from rdflib import Graph, URIRef, RDF, RDFS, Literal
    g = Graph()
    g.parse(path, format="turtle")
    songs = list(g.subjects(RDF.type, URIRef(VAL + "Song")))
    if len(songs) != 1:
        raise ValueError("%s: a song file holds one val:Song, this holds %d" % (path, len(songs)))
    node = songs[0]
    v = lambda name: URIRef(VAL + name)

    def one(subject, name, default=None, cast=float):
        o = g.value(subject, v(name))
        return cast(o) if o is not None else default

    song = Song(path)
    song.title = one(node, "title", cast=str) or str(g.value(node, RDFS.label) or _local(node))
    song.name = _local(node)
    song.tempo = one(node, "tempo")
    if song.tempo is None:
        raise ValueError("%s: the song has no val:tempo" % path)
    song.beats_per_bar = int(one(node, "beatsPerBar", 4))
    song.loudness = one(node, "loudness")
    song.tail = one(node, "tailSeconds", 10.0)
    song.fade = one(node, "fadeSeconds", 4.0)
    song.room = one(node, "roomAmount", 0.10)
    song.drive = one(node, "drive", 1.3)
    song.room_decay = one(node, "roomDecay", 0.4)
    song.room_lowpass = one(node, "roomLowpass", 4500.0)

    found = sorted(((int(one(s, "index")), s) for s in g.objects(node, v("hasSection"))), key=lambda t: t[0])
    first = 0
    for index, s in found:
        label = g.value(s, RDFS.label)
        bars = int(one(s, "bars"))
        song.sections.append(Section(_local(s), str(label) if label is not None else _local(s), index, bars,
                                     one(s, "level", 0.0), first))
        first += bars

    def stems(predicate, is_return):
        out = []
        for p in g.objects(node, v(predicate)):
            name = _local(p)
            source = g.value(p, v("source"))
            named = g.value(p, v("midi"))
            midi = (str(named) if named is not None else None) if is_return else str(named or "midi/%s.mid" % name)
            follows = g.value(p, v("follows"))
            out.append((one(p, "index", 1.0e9), name, Stem(name, is_return, str(g.value(p, v("circuit"))), midi,
                                                           _local(source) if source is not None else None,
                                                           one(p, "level", -20.0), one(p, "pan", 0.0), one(p, "highpass", 0.0))))
            out[-1][2].follows = _local(follows) if follows is not None else None
        return [t[2] for t in sorted(out, key=lambda t: (t[0], t[1]))]

    song.stems = stems("hasPart", False) + stems("hasReturn", True)
    return song


# -- rendering and mixing, by valis-song ---------------------------------------------------------------------

def _newest_source():
    newest = 0.0
    for top in ("src", "include", "vocabs"):
        for root, _, names in os.walk(os.path.join(VALIS, top)):
            for n in names:
                newest = max(newest, os.path.getmtime(os.path.join(root, n)))
    return newest


def pick_tool():
    """Release is several times faster and gives the same samples, but a binary older than the source runs the old code
    (this once rendered a stem full of NaN that the source had already fixed), so a stale one is passed over.
    VALIS_SONG names a binary outright."""
    if os.environ.get("VALIS_SONG"):
        return os.environ["VALIS_SONG"]
    newest = _newest_source()
    for d, c in (("build-release", "Release"), ("build", "Debug")):
        p = os.path.join(VALIS, d, "valis_song_artefacts", c, "valis_song")
        if os.path.exists(p):
            if os.path.getmtime(p) >= newest:
                return p
            print("skipping %s: older than the source (rebuild it, or set VALIS_SONG)" % p, file=sys.stderr)
    sys.exit("no valis_song newer than the source: run cmake --build build")


_tool = None


def _run(args):
    """Runs valis-song from the Valis repository, so sound files that circuits name relative to it resolve, and returns
    the JSON it prints. A failure is its message on standard error."""
    global _tool
    if _tool is None:
        _tool = pick_tool()
    r = subprocess.run([_tool] + args, cwd=VALIS, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit((r.stderr or r.stdout).strip() or "valis-song failed")
    start = r.stdout.find("{")          # JUCE prints a banner line first
    return json.loads(r.stdout[start:])


def check(song):
    """The song as the C++ reader sees it, with `problems` listing files it names that are missing."""
    return _run(["check", song.path])


def score(song, only=None):
    """Plays the song's score (val:score) into the MIDI file of each part it plays; a part it does not play is left alone."""
    args = ["score", song.path]
    if only:
        args += ["--only", ",".join(only)]
    for p in _run(args)["parts"]:
        print("%-12s %5d notes" % (p["part"], p["notes"]))


def render(song, only=None, workers=3):
    """Renders every part (a stem is the part's circuit played by its MIDI file), then every return (the source stem
    through the return's circuit). `only` is a list of part names; a return is redone with its source."""
    args = ["render", song.path, "--workers", str(workers)]
    if only:
        args += ["--only", ",".join(only)]
    for s in _run(args)["stems"]:
        print("%-12s %s" % (s["name"], "%.1f s, peak %.3f, rms %.4f%s" % (s["seconds"], s["peak"], s["rms"], "  CLIPPED" if s["clipped"] else "")))


def mix(song, version="v1", target_lufs=None, arc=False, overwrite=False):
    """Mixes the stems to mixes/<slug>-<version>.wav: levelled to each stem's val:level, high-passed, panned and summed;
    then the sections' levels if `arc`, a fade, a room, a soft clip and the peak at -1 dB, then the loudness the song
    asks for unless target_lufs says otherwise. Will not replace a mix of that version unless `overwrite`. A reference
    mix: vocals and mastering are done in a DAW."""
    args = ["mix", song.path, "--version", version]
    if arc:
        args.append("--arc")
    if overwrite:
        args.append("--overwrite")
    if target_lufs is not None:
        args += ["--lufs", str(target_lufs)]
    r = _run(args)
    for name in r["missing"]:
        print("missing", name)
    if "gainDb" in r:
        print("loudness: gain %+.2f dB toward %.1f LUFS%s" % (r["gainDb"], r["targetLufs"], " (held back by the peak ceiling)" if r["limitedByPeak"] else ""))
    print("mixed:", r["path"])
    l = r["loudness"]
    print("loudness: integrated %.1f LUFS, range %.1f LU, true peak %.1f dBTP" % (l["integratedLufs"], l["rangeLu"], l["truePeakDbtp"]))
    print("section levels (playing rms, dB):", ", ".join("%s %.1f" % (s["name"], s["level"]) for s in r["sections"]))
    return r["path"]
