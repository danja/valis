---
name: release-pack
description: Make the words and the sleeve for a track: lyrics laid out against its bars, a guide melody, a sleeve design, and the notes that go with them. Use when asked for lyrics, a record sleeve, cover art or a release for a Valis song.
---

# Lyrics, guide melody and sleeve

Examples: `docs/dead-at-midnight-lyrics.md` and `-sleeve.svg`/`.png`; `docs/short-circuit-lyrics.md`,
`docs/short-circuit-sleeve.md`, `.html` and `.png`; `scripts/electro83/vocal-guide.py`.

## Lyrics

1. Fit them to the arrangement's bar table. Each line is two bars; sections are named with their bar numbers
   counted from 1. Write the instrumental and spoken sections into the document too, so nothing is left to guess.
2. Keep lines to 6 to 9 syllables and write the syllables apart with hyphens in the guide script.
3. The words should be about the same thing as the sleeve. Say in the document what the idea is.
4. No vocal is made. Say so, and offer the guide melody.

## Guide melody

`vocal-guide.py` writes one note per syllable on the eighth-note grid, from the scale of the key, near one pitch,
leaning up at the end of a line. Render it with `examples/emulator-choir.ttl`. It is not in the mix.

## Sleeve

1. Decide the idea in one sentence before drawing. The 1983 pair: a sleeve imitating the technology of its own
   day (a floppy disk) became a microSD card, a technology 22 years too late.
2. Use an invented artist, label and catalogue number. No real band's name, label, logo or mark.
3. Draw it in the design artifact (`Artifact` tool, quickstart intent "design"), front and back, 840 by 840 pixels
   for 12 inches. Type in a geometric sans (Jost). Information in a code or a specification rather than a headline.
4. **Save it in the repository.** Write a standalone HTML page (no artifact runtime), render it with
   `google-chrome --headless --no-sandbox --screenshot=... --window-size=1840,960 file://...` for a PNG, and put
   both under `docs/` with a short description page. Read the PNG to check the layout.
5. The artifact link is private until the owner shares it; say so.
