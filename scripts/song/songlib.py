# scripts/song/songlib.py
#
# What every song script shares: reading a song file (a Turtle file with one val:Song, see docs/songs.md), finding a
# renderer that is not out of date, rendering the parts, and mixing the stems. A track's own script keeps only its score.
#
#   import songlib
#   song = songlib.load("artifacts/my-song/song.ttl")
#   songlib.render(song)                       # every part, then the returns
#   songlib.mix(song, version="v1", arc=True)  # the mixdown named by the song's title, at the song's loudness
#
# The same file is checked, with located reasons, by the MCP tool read_song and by Song::read in C++. This reader only
# reads: run read_song first if a file might be wrong.
import os, sys, subprocess
from concurrent.futures import ThreadPoolExecutor

VALIS = os.environ.get("VALIS", os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")))
sys.path.insert(0, os.path.join(VALIS, "scripts", "shot"))

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
        self.sections, self.stems = [], []

    # -- the clock
    @property
    def bars(self):
        return sum(s.bars for s in self.sections)

    @property
    def bar_seconds(self):
        return self.beats_per_bar * 60.0 / self.tempo

    @property
    def seconds(self):
        return self.bars * self.bar_seconds

    @property
    def render_seconds(self):
        return self.seconds + self.tail

    def locate(self, bar):
        """(section, bar within it) for a 0-based bar."""
        for s in self.sections:
            if bar < s.first_bar + s.bars:
                return s, bar - s.first_bar
        raise ValueError(bar)

    # -- where things are
    def path_of(self, relative):
        return relative if os.path.isabs(relative) else os.path.join(self.dir, relative)

    @property
    def midi_dir(self):
        return os.path.join(self.dir, "midi")

    @property
    def wav_dir(self):
        return os.path.join(self.dir, "wavs")

    @property
    def mix_dir(self):
        return os.path.join(self.dir, "mixes")

    @property
    def parts(self):
        return [s for s in self.stems if not s.is_return]

    @property
    def returns(self):
        return [s for s in self.stems if s.is_return]

    @property
    def slug(self):
        """The title as a file name: lower case words joined by hyphens."""
        out, dash = "", False
        for c in self.title.lower():
            if c.isalnum():
                out += ("-" if dash and out else "") + c
                dash = False
            else:
                dash = True
        return out or "song"


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
            midi = None if is_return else str(g.value(p, v("midi")) or "midi/%s.mid" % name)
            out.append((one(p, "index", 1.0e9), name, Stem(name, is_return, str(g.value(p, v("circuit"))), midi,
                                                           _local(source) if source is not None else None,
                                                           one(p, "level", -20.0), one(p, "pan", 0.0), one(p, "highpass", 0.0))))
        return [t[2] for t in sorted(out, key=lambda t: (t[0], t[1]))]

    song.stems = stems("hasPart", False) + stems("hasReturn", True)
    return song


# -- rendering -------------------------------------------------------------------------------------------------

def _newest_source():
    newest = 0.0
    for top in ("src", "include", "vocabs"):
        for root, _, names in os.walk(os.path.join(VALIS, top)):
            for n in names:
                newest = max(newest, os.path.getmtime(os.path.join(root, n)))
    return newest


def pick_renderer():
    """Release is several times faster and renders the same samples, but a binary older than the source renders the
    old code (this once rendered a stem full of NaN that the source had already fixed), so a stale one is passed over.
    VALIS_RENDER names a binary outright."""
    if os.environ.get("VALIS_RENDER"):
        return os.environ["VALIS_RENDER"]
    newest = _newest_source()
    for d, c in (("build-release", "Release"), ("build", "Debug")):
        p = os.path.join(VALIS, d, "valis_render_artefacts", c, "valis_render")
        if os.path.exists(p):
            if os.path.getmtime(p) >= newest:
                return p
            print("skipping %s: older than the source (rebuild it, or set VALIS_RENDER)" % p, file=sys.stderr)
    sys.exit("no valis_render newer than the source: run cmake --build build")


_renderer = None


def renderer():
    """Chosen on first use, so mixing alone never needs a built renderer."""
    global _renderer
    if _renderer is None:
        _renderer = pick_renderer()
    return _renderer


def _run(args):
    # cwd is the Valis repository, so sound files that circuits name relative to it resolve
    r = subprocess.run([renderer()] + args, cwd=VALIS, capture_output=True, text=True)
    return r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr[-300:]


def render(song, only=None, workers=3):
    """Renders every part (a stem is the part's circuit played by its MIDI file), then every return (the source stem
    through the return's circuit). `only` is a list of part names; a return is redone with its source."""
    os.makedirs(song.wav_dir, exist_ok=True)

    def part(stem):
        midi = song.path_of(stem.midi)
        if not os.path.exists(midi):
            print("missing", midi)
            return
        print(stem.name, _run([song.path_of(stem.circuit), "--midi", midi, "--tempo", str(song.tempo), "--rolling",
                               "--seconds", "%.1f" % song.render_seconds, "-o", "%s/%s.wav" % (song.wav_dir, stem.name)]),
              flush=True)

    def effect(stem):
        print(stem.name, _run([song.path_of(stem.circuit), "-i", "%s/%s.wav" % (song.wav_dir, stem.source),
                               "-o", "%s/%s.wav" % (song.wav_dir, stem.name)]), flush=True)

    with ThreadPoolExecutor(max_workers=workers) as pool:
        list(pool.map(part, [s for s in song.parts if only is None or s.name in only]))
        list(pool.map(effect, [s for s in song.returns if only is None or s.source in only]))


# -- mixing ----------------------------------------------------------------------------------------------------

def playing_rms_db(x, rate):
    """RMS in dB over the tenth-second windows that sound (above 0.001), so a sparse part is measured by when it plays."""
    import numpy as np
    w = rate // 10
    n = len(x) // w
    e = np.sqrt((x[:n * w].reshape(n, w) ** 2).mean(axis=1))
    e = e[e > 1.0e-3]
    return 20 * np.log10(np.sqrt((e ** 2).mean()) + 1.0e-12) if len(e) else -120.0


def room(x, rate):
    """A synthetic room: exponentially decaying noise, different in each ear, convolved with the whole band."""
    import numpy as np
    from scipy.signal import fftconvolve, butter, sosfilt
    rng = np.random.default_rng(7)
    n = int(1.6 * rate)
    t = np.arange(n) / rate
    env = np.exp(-t / 0.4) * (1.0 - np.exp(-t / 0.004))
    ir = np.stack([rng.standard_normal(n) * env, rng.standard_normal(n) * env], axis=1)
    ir = sosfilt(butter(1, 4500, "lowpass", fs=rate, output="sos"), ir, axis=0)
    ir /= np.sqrt((ir ** 2).sum(axis=0))
    ir = np.concatenate([np.zeros((int(0.012 * rate), 2)), ir])
    wet = np.stack([fftconvolve(x[:, 0], ir[:, 0]), fftconvolve(x[:, 1], ir[:, 1])], axis=1)
    return wet[:len(x)]


def arc_curve(song, rate, length):
    """Per-sample gain (linear) that steps between the sections' levels, smoothed over about two seconds."""
    import numpy as np
    curve = np.zeros(length)
    at = 0
    for s in song.sections:
        end = at + int(s.bars * song.bar_seconds * rate)
        curve[at:min(end, length)] = s.level
        at = end
    curve[at:] = song.sections[-1].level
    w = int(2.0 * rate) | 1
    padded = np.concatenate([np.full(w // 2, curve[0]), curve, np.full(w // 2, curve[-1])])
    running = np.concatenate([[0.0], np.cumsum(padded)])    # a moving average in one pass, not length * w multiplies
    smooth = (running[w:] - running[:-w]) / w
    return 10 ** (smooth / 20)


def section_levels(song, x, rate):
    """Playing rms in dB of each section, so an arc can be checked by number."""
    out, at = [], 0
    for s in song.sections:
        end = at + int(s.bars * song.bar_seconds * rate)
        seg = x[at:end]
        out.append((s.name, playing_rms_db(seg if seg.ndim == 1 else seg.mean(axis=1), rate)))
        at = end
    return out


def mix(song, version="v1", target_lufs=None, arc=False, fade=4.0, room_amount=0.10, drive=1.3):
    """Mixes the stems to mixes/<slug>-<version>.wav. Each stem is levelled so its playing rms is the stem's val:level,
    high-passed, panned (a stereo stem keeps its image and takes the pan as a balance) and summed; then the sections'
    levels if `arc`, a fade, a synthetic room, a soft clip and the peak set to -1 dB, then the loudness the song asks
    for unless target_lufs says otherwise. A reference mix: vocals and mastering are done in a DAW."""
    import numpy as np
    from scipy.io import wavfile
    from scipy.signal import butter, sosfilt
    import lufs
    os.makedirs(song.mix_dir, exist_ok=True)
    total = None
    for stem in song.stems:
        path = "%s/%s.wav" % (song.wav_dir, stem.name)
        if not os.path.exists(path):
            print("missing", path)
            continue
        rate, d = wavfile.read(path)
        d = d.astype(np.float64) / (2.0 ** 31 if d.dtype == np.int32 else 32768.0 if d.dtype == np.int16 else 1.0)
        if d.ndim == 1:
            d = np.stack([d, d], axis=1)
        if stem.highpass:
            d = sosfilt(butter(2, stem.highpass, "highpass", fs=rate, output="sos"), d, axis=0)
        g = 10 ** ((stem.level - playing_rms_db(d.mean(axis=1), rate)) / 20)
        theta = (stem.pan + 1) * np.pi / 4
        stereo = d * np.array([np.cos(theta), np.sin(theta)]) * g * np.sqrt(2)
        if total is None:
            total = np.zeros((max(len(stereo), int(song.render_seconds * rate)), 2))
        total[:len(stereo)] += stereo
    if total is None:
        raise SystemExit("no stems to mix in %s" % song.wav_dir)
    total = total[:int((song.seconds + 4) * rate)]
    if arc:
        total = total * arc_curve(song, rate, len(total))[:, None]
    total[-int(fade * rate):] *= np.linspace(1.0, 0.0, int(fade * rate))[:, None]
    total = total + room(total, rate) * room_amount
    mx = np.tanh(total * drive) / drive
    mx = mx / np.max(np.abs(mx)) * 10 ** (-1.0 / 20)
    out = "%s/%s-%s.wav" % (song.mix_dir, song.slug, version)
    wavfile.write(out, rate, (mx * 8388607).astype(np.int32) << 8)
    target = target_lufs if target_lufs is not None else song.loudness
    if target is not None:
        gain, limited = lufs.gain_to_target(out, target)
        mx = mx * 10 ** (gain / 20)
        wavfile.write(out, rate, (mx * 8388607).astype(np.int32) << 8)
        print("loudness: gain %+.2f dB toward %.1f LUFS%s" % (gain, target, " (held back by the peak ceiling)" if limited else ""))
    print("mixed:", out)
    print("loudness:", lufs.measure(out))
    print("section levels (playing rms, dB):", ", ".join("%s %.1f" % lv for lv in section_levels(song, mx, rate)))
    return out
