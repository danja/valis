# scripts/electro83/vocal-guide.py
#
# A guide melody for the lyrics of "Tenement Lights (Short Circuit)": one MIDI note per syllable, so a
# singer can hear where each line sits. The lyrics are in docs/short-circuit-lyrics.md; the bars here
# are those of track.py --short. The guide is rendered by examples/emulator-choir.ttl to
# short-circuit/vocal-guide.wav and is not in the mix.
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "halloween"))
import midiw

OUT = os.path.expanduser("~/Music/tenement-lights/short-circuit")
BPM = 130
SCALE = [57, 59, 60, 62, 64, 65, 67, 69, 71, 72]       # A natural minor, A3 to C5
ANCHOR = [4, 4, 3, 3, 2, 2, 3, 4]                       # a scale index per bar of the eight-bar cycle: E, E, D, D, C, C, D, E
CONTOUR = [0, 0, 1, 0, -1, 0, 1, 2, 1]                  # a voice that stays near one note and leans up at the end of a line

# Each line is two bars. Syllables are separated by spaces.
VERSE_1 = ["The lights go out a-long the street", "Ex-cept for one that's burn-ing low",
           "I kept your voice on some-thing small", "Too small to hold what it con-tains",
           "You said that no-thing lasts that long", "You said we'd on-ly have to-night",
           "But I have it here in my hand", "And it weighs no more than a breath"]
VERSE_2 = ["They'll find it in a dif-fer-ent year", "Be-hind the plas-ter un-der the stairs",
           "A thing too neat to be from now", "Too bright to be a thing we made",
           "And some-one there will press a key", "And hear us in the room a-gain",
           "Still count-ing win-dows one by one", "Still wait-ing for the light to change"]
CHORUS = ["Keep it small keep it safe", "Keep it where the dark can't reach",
          "Play it back when we are gone", "Play it back and it's to-night"]
TAG = ["Play it back and it's to-night"]

# (first bar, lines) in the short arrangement: verse 1 over the groove, verse 2 over the lift,
# the chorus twice over the peak, and the last line twice in the outro.
PLAN = [(8, VERSE_1), (24, VERSE_2), (48, CHORUS), (56, CHORUS), (80, TAG + TAG)]


def line_notes(bar, text):
    syllables = text.replace("-", " ").split()
    anchor = ANCHOR[bar % 8]
    notes = []
    for k, _ in enumerate(syllables):
        deg = min(max(anchor + CONTOUR[k % len(CONTOUR)], 0), len(SCALE) - 1)
        start = bar * 4.0 + k * 0.5
        length = 0.45 if k < len(syllables) - 1 else 2.5
        notes.append((start, length, SCALE[deg], 96 if k % 2 == 0 else 84))
    return notes


notes = []
for first, lines in PLAN:
    for n, text in enumerate(lines):
        notes += line_notes(first + n * 2, text)
os.makedirs(OUT + "/midi", exist_ok=True)
midiw.write(OUT + "/midi/vocal.mid", sorted(notes), BPM)
print(len(notes), "syllables")
