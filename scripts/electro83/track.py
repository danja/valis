# scripts/electro83/track.py
#
# "Tenement Lights": an original instrumental in the manner of New Order's 1983 work, played
# by the circuits in examples/ (dmx.ttl, moog-source.ttl, six-string-bass.ttl, prophet-5.ttl,
# emulator-choir.ttl, quadra-strings.ttl, speaker-room.ttl). Nothing in it is taken from
# the record: the notes, patterns and form are new. See docs/new-order-track.md.
#
#   python3 track.py [--short] score    write one MIDI file per part
#   python3 track.py [--short] render   render every part with valis-render (a few minutes)
#   python3 track.py [--short] mix      mix the stems
#   python3 track.py [--short]          all three
#
# Key A minor, 130 bpm. The full arrangement is 224 bars (about 6:53) and goes to
# ~/Music/tenement-lights/. --short is the three-minute arrangement with a vocal, 96 bars, and
# goes to ~/Music/tenement-lights/short-circuit/. The vocal's lyrics are in docs/short-circuit-lyrics.md.
import os, sys, subprocess, random
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts", "halloween"))
import midiw

SHORT = "--short" in sys.argv
NAME = "short-circuit" if SHORT else "tenement-lights"
OUT = os.path.expanduser("~/Music/tenement-lights" + ("/short-circuit" if SHORT else ""))
# Release is several times faster and renders the same samples; fall back to Debug when only that is built.
RENDER = next((p for p in (os.path.join(ROOT, d, "valis_render_artefacts", c, "valis_render")
                           for d, c in (("build-release", "Release"), ("build", "Debug"))) if os.path.exists(p)),
              os.path.join(ROOT, "build", "valis_render_artefacts", "Debug", "valis_render"))
BPM = 130
STEP = 0.25                       # a sixteenth, in beats
SECTIONS = ([("intro", 8), ("groove", 16), ("lift", 16), ("break", 8), ("peak", 24), ("fx", 8), ("outro", 16)] if SHORT else
            [("intro", 32), ("groove", 32), ("lift", 32), ("break", 16),
             ("peak", 48), ("fx", 8), ("strip", 32), ("outro", 24)])
SWING = 0.54                      # the drum machine's shuffle: the second sixteenth of each pair sits at 54%
BARS = sum(n for _, n in SECTIONS)
SECONDS = BARS * 4 * 60 / BPM + 8
PARTS = ["kick", "drums", "perc", "wood", "source", "hook", "prophet", "choir", "strings"]
DRUMS = ("kick", "drums", "perc", "wood")

# Eight bars of harmony, then round again: Am Am G G F F G E.
ROOTS  = [33, 33, 31, 31, 29, 29, 31, 28]
TRIADS = [(57, 60, 64, 69), (57, 60, 64, 69), (55, 59, 62, 67), (55, 59, 62, 67),
          (53, 57, 60, 65), (53, 57, 60, 65), (55, 59, 62, 67), (52, 56, 59, 64)]
STRINGS = [(45, 57, 60, 64, 72), (45, 57, 60, 64, 72), (43, 55, 59, 62, 71), (43, 55, 59, 62, 71),
           (41, 53, 57, 60, 69), (41, 53, 57, 60, 69), (43, 55, 59, 62, 71), (40, 52, 56, 59, 68)]
# The melody the bass guitar plays: (beat in bar, length, note) for each of the eight bars.
HOOK = [[(0, 1.5, 64), (1.5, .5, 67), (2, 1, 69), (3, 1, 72)],
        [(0, 2, 71), (2, 1, 69), (3, 1, 64)],
        [(0, 1.5, 62), (1.5, .5, 67), (2, 1, 71), (3, 1, 74)],
        [(0, 2, 72), (2, 1, 71), (3, 1, 67)],
        [(0, 1.5, 60), (1.5, .5, 65), (2, 1, 69), (3, 1, 72)],
        [(0, 2, 69), (2, 1, 67), (3, 1, 65)],
        [(0, 1.5, 62), (1.5, .5, 67), (2, 2, 71)],
        [(0, 1, 64), (1, 1, 68), (2, 1, 71), (3, 1, 76)]]
# Prophet arpeggio: which chord tone on each sixteenth, None for a rest.
ARP = [0, None, 2, 3, None, 2, 1, None, 0, None, 2, 3, 2, None, 1, 2]

K_INTRO = {0: 127, 2: 70, 3: 70, 4: 110, 6: 70, 7: 70, 8: 127, 10: 70, 11: 70, 12: 110, 14: 70, 15: 70}
K_MAIN  = {0: 127, 2: 70, 4: 115, 7: 75, 8: 127, 10: 70, 11: 70, 12: 115, 14: 80, 15: 90}


def locate(bar):
    start = 0
    for name, n in SECTIONS:
        if bar < start + n:
            return name, bar - start, n
        start += n


def build():
    rnd = random.Random(1983)
    perf = {p: [] for p in PARTS}

    # A note shorter than its slot leaves the gate down for a moment, so the next one starts its
    # envelopes again; notes that touch would play legato and a plucked string would not speak.
    gate = {"source": 0.75, "prophet": 0.8, "hook": 0.85}

    def add(part, bar, step, length_steps, note, vel):
        length = length_steps * STEP
        length = length * gate[part] if part in gate and part != "hook" else (length - 0.1 if part == "hook" else length)
        start = (bar * 16 + step) * STEP
        if part in DRUMS and step % 2 == 1:
            start += (SWING - 0.5) * 2 * STEP          # a pair lasts half a beat; its second hit moves to SWING of that
        perf[part].append((start, length, note, int(vel)))

    pads = {"choir": [], "strings": []}

    def pad(part, bar, notes, vel):
        chord = tuple(notes)
        run = pads[part]
        if run and run[-1][0] == chord and run[-1][2] == bar:
            run[-1][2] = bar + 1
        else:
            run.append([chord, bar, bar + 1, vel])

    for bar in range(BARS):
        sec, i, n = locate(bar)
        c = bar % 8
        root = ROOTS[c]
        last = i == n - 1
        eighth_group = (i % 8 == 7)

        # ---- kick: the sixteenth-note engine
        kick_on = sec in ("intro", "groove", "lift", "peak", "fx") or (sec == "strip" and i < 16) or (sec == "outro" and i < 8)
        if kick_on:
            pattern = K_INTRO if sec == "intro" and i < 16 else K_MAIN
            for st, v in pattern.items():
                add("kick", bar, st, 1, 36 if v > 100 else 35, v if sec != "intro" or i >= 2 else v * 0.8)
            if eighth_group and sec in ("lift", "peak"):
                for st in (13, 14, 15):
                    add("kick", bar, st, 1, 35, 85 + st * 2)

        # ---- the rest of the kit
        if sec != "break" and not (sec == "outro" and i >= 12) and not (sec == "strip" and i >= 24) \
                and not (sec == "intro" and i < 8):
            for st in range(0, 16, 2 if sec in ("intro", "groove") else 1):
                if st % 2 == 0:
                    add("drums", bar, st, 1, 42, (82 if st % 4 == 0 else 58) + rnd.randint(-6, 6))
                elif sec in ("peak", "fx"):
                    add("drums", bar, st, 1, 42, 38 + rnd.randint(-4, 6))
            if sec != "intro":
                for st in (2, 6, 10, 14):
                    add("drums", bar, st, 2, 46, 68 if st in (6, 14) else 50)
        if sec in ("groove", "lift", "peak", "fx", "strip", "outro") and not (sec == "strip" and i >= 24) and not (sec == "outro" and i >= 12):
            for st in (4, 12):
                add("drums", bar, st, 2, 39, 112)
            if sec in ("lift", "peak", "fx"):
                add("drums", bar, 12, 2, 38, 96)
                if i % 2 == 1:
                    add("drums", bar, 15, 1, 39, 80)
        if sec in ("lift", "peak", "fx"):
            for st in range(16):
                add("perc", bar, st, 1, 69, 30 + (14 if st % 4 == 2 else 0) + rnd.randint(0, 8))
        if sec in ("peak",):
            for st in (4, 12):
                add("perc", bar, st, 1, 54, 70)
        if sec in ("groove", "lift", "peak") and i % 2 == 1:
            for k in range(3):                              # a triplet across the last beat
                perf["wood"].append(((bar * 4 + 3 + k / 3.0), 0.12, 37, 64 + k * 6))
        if eighth_group and sec in ("lift", "peak", "fx", "groove"):
            for st, tom in zip(range(8, 16), (48, 48, 47, 47, 45, 45, 45, 45)):
                add("drums", bar, st, 1, tom, 80 + (st - 8) * 5)

        # ---- the Source bass
        bass_on = sec in ("groove", "lift", "peak", "fx") or (sec == "strip" and i < 24) or (sec == "outro" and i < 8)
        if bass_on:
            if i % 4 == 3:
                tail = {12: 0, 13: 3, 14: 7, 15: 10}
            else:
                tail = None
            for beat in range(4):
                b = beat * 4
                if tail and beat == 3:
                    for st, off in tail.items():
                        add("source", bar, st, 1, root + off, 105)
                else:
                    add("source", bar, b, 1, root, 118)
                    add("source", bar, b + 2, 1, root + 12, 96)
                    add("source", bar, b + 3, 1, root, 100)

        # ---- the bass guitar's tune
        if sec in ("break", "peak", "outro") or (sec == "strip" and i < 16):
            if not (sec == "outro" and i >= 12):
                for beat, length, note in HOOK[c]:
                    add("hook", bar, int(beat * 4), int(length * 4), note, 104 if beat == 0 else 92)

        # ---- the Prophet's arpeggio; from the peak it runs a sixteenth late, one rest too many
        if sec in ("lift", "peak") or (sec == "outro" and i < 8):
            shift = 1 if sec == "peak" else 0
            for st, tone in enumerate(ARP):
                if tone is None:
                    continue
                s2 = st + shift
                nb = bar + s2 // 16
                add("prophet", nb, s2 % 16, 1, TRIADS[c][tone], 100 if st % 4 == 0 else 84)

        # ---- the choir and the strings hold the chord
        if sec in ("break", "peak") or (sec == "strip") or (sec == "outro" and i >= 8) or (SHORT and sec == "intro"):
            pad("choir", bar, TRIADS[c], 84)
        if sec in ("break", "peak", "outro") or (sec == "lift" and i >= n // 2):
            pad("strings", bar, STRINGS[c], 78)

    for part, runs in pads.items():
        for chord, b0, b1, vel in runs:
            for note in chord:
                perf[part].append((b0 * 4.0, (b1 - b0) * 4.0 - 0.05, note, vel))
    return perf


def score():
    os.makedirs(OUT + "/midi", exist_ok=True)
    perf = build()
    for part, notes in perf.items():
        midiw.write("%s/midi/%s.mid" % (OUT, part), sorted(notes), BPM)
        print("%-8s %5d notes" % (part, len(notes)))


JOBS = [  # part, circuit
    ("kick", "examples/dmx.ttl"), ("drums", "examples/dmx.ttl"),
    ("perc", "examples/dmx.ttl"), ("wood", "examples/dmx.ttl"),
    ("source", "examples/moog-source.ttl"), ("hook", "examples/six-string-bass.ttl"),
    ("prophet", "examples/prophet-5.ttl"), ("choir", "examples/emulator-choir.ttl"),
    ("strings", "examples/quadra-strings.ttl")]


def run_render(job):
    part, circuit = job
    cmd = [RENDER, circuit, "--midi", "%s/midi/%s.mid" % (OUT, part), "--tempo", str(BPM), "--rolling",
           "--seconds", "%.1f" % SECONDS, "-o", "%s/%s.wav" % (OUT, part)]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    print(part, r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr[-300:], flush=True)


def render(only=None):
    with ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(run_render, [j for j in JOBS if only is None or j[0] in only]))
    # The kick is also played through a loudspeaker and recorded again from the room.
    import numpy as np
    from scipy.io import wavfile
    rate, kick = wavfile.read(OUT + "/kick.wav")
    wavfile.write(OUT + "/kick-in.wav", rate, kick)
    cmd = [RENDER, "examples/speaker-room.ttl", "-i", OUT + "/kick-in.wav", "--seconds", "%.1f" % SECONDS,
           "-o", OUT + "/kick-room.wav"]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    print("kick-room", r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr[-300:])

    # The high percussion, the wood block most of all, goes to a reverb of its own.
    _, wood = wavfile.read(OUT + "/wood.wav")
    _, perc = wavfile.read(OUT + "/perc.wav")
    send = 0.5 * (wood.astype(np.float64) + 0.5 * perc.astype(np.float64))   # half, so the sum cannot clip
    wavfile.write(OUT + "/high-perc-in.wav", rate, send.astype(np.int32))
    cmd = [RENDER, "examples/reverb-return.ttl", "-i", OUT + "/high-perc-in.wav", "--seconds", "%.1f" % SECONDS,
           "-o", OUT + "/high-perc-room.wav"]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    print("high-perc-room", r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr[-300:])


# part: (level in dB of the stem's playing rms, pan -1..1, high-pass Hz or 0). The percussion
# stems and the reverb on them take the drum kit's gain and add the dB given here, so they keep
# the balance they were played at.
FOLLOWS = {"perc", "wood", "high-perc-room"}
LEVELS = {"kick": (-19.0, 0.0, 0), "kick-room": (-22.5, 0.0, 0), "drums": (-23.0, 0.0, 0),
          "perc": (0.0, 0.0, 0), "wood": (0.0, 0.0, 0), "high-perc-room": (-4.0, 0.0, 0),
          "source": (-19.5, 0.0, 0), "hook": (-23.0, -0.15, 0), "prophet": (-24.0, 0.3, 0),
          "choir": (-28.0, -0.25, 140), "strings": (-29.0, 0.2, 120)}


def playing_rms_db(x, rate):
    """RMS of the stretches that are not silent, so a part that plays rarely sits as loud as one that always does."""
    import numpy as np
    w = rate // 10
    n = len(x) // w
    e = np.sqrt((x[:n * w].reshape(n, w) ** 2).mean(axis=1))
    e = e[e > 1.0e-3]
    return 20 * np.log10(np.sqrt((e ** 2).mean()) + 1.0e-12) if len(e) else -120.0


def mix():
    import numpy as np
    from scipy.io import wavfile
    from scipy.signal import butter, sosfilt
    total = None
    kit_gain = 1.0
    for part, (level, pan, hp) in LEVELS.items():
        path = "%s/%s.wav" % (OUT, part)
        if not os.path.exists(path):
            print("missing", path)
            continue
        rate, d = wavfile.read(path)
        d = d.astype(np.float64) / (2.0 ** 31 if d.dtype == np.int32 else 32768.0 if d.dtype == np.int16 else 1.0)
        mono = d if d.ndim == 1 else d.mean(axis=1)
        if hp:
            mono = sosfilt(butter(2, hp, "highpass", fs=rate, output="sos"), mono)
        if part in FOLLOWS:
            g = 10 ** ((level + 0.0) / 20) * kit_gain
        else:
            g = 10 ** ((level - playing_rms_db(mono, rate)) / 20)
            if part == "drums":
                kit_gain = g
        if part in ("drums", "perc", "wood") and d.ndim == 2:
            stereo = d * g
        else:
            theta = (pan + 1) * np.pi / 4
            stereo = np.stack([mono * np.cos(theta), mono * np.sin(theta)], axis=1) * g * np.sqrt(2)
        if total is None:
            total = np.zeros((max(len(stereo), int(SECONDS * rate)), 2))
        total[:len(stereo)] += stereo
    if SHORT:                                              # three minutes, the last three seconds a fade
        total = total[:int(180 * rate)]
        total[-3 * rate:] *= np.linspace(1.0, 0.0, 3 * rate)[:, None]
    mx = np.tanh(total * 1.8) / 1.8                        # the desk, gently
    mx = mx / np.max(np.abs(mx)) * 10 ** (-1.0 / 20)
    wavfile.write("%s/%s.wav" % (OUT, NAME), rate, (mx * 8388607).astype(np.int32) << 8)
    print("mixed: peak %.3f rms %.3f dB" % (np.max(np.abs(mx)), 20 * np.log10(np.sqrt(np.mean(mx ** 2)))))


if __name__ == "__main__":
    steps = [a for a in sys.argv[1:] if not a.startswith("--")] or ["score", "render", "mix"]
    for s in steps:
        {"score": score, "render": render, "mix": mix}[s]()
