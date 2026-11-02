#!/usr/bin/env python3
# scripts/catalogue.py
#
# Writes a searchable catalogue of circuits/*.ttl as one static page (catalogue.html) and a data file
# (catalogue.json). Needs rdflib. Usage: scripts/catalogue.py [out-dir]   (default _site)

import json, re, sys
from pathlib import Path
from rdflib import Graph, Namespace, RDF, RDFS

import catalogue_elements

VAL = Namespace("http://purl.org/stuff/valis/")
TRN = Namespace("http://purl.org/stuff/transmissions/")
REPO = "https://github.com/danja/valis/blob/main/circuits/"
ROOT = Path(__file__).resolve().parent.parent

ROLES = {"AudioInstrument": "Instrument", "Instrument": "Instrument", "DrumInstrument": "Drums",
         "AudioEffect": "Effect", "Effect": "Effect"}
NOT_ELEMENTS = {"Circuit", "Arc", "Param", "Subcircuit", "Node"}


def local(iri):
    return re.split(r"[#/]", str(iri).rstrip("/"))[-1]


def summary(text):
    """The first paragraph of the header comment, after the line that names the file."""
    out, started = [], False
    for line in text.splitlines():
        if not line.startswith("#"):
            if not started and (not line.strip() or line.startswith("@prefix")):
                continue
            break
        t = line[1:].strip()
        if not started:
            if not t or t.startswith("circuits/"):
                continue
            started = True
        elif not t:
            break
        out.append(t)
    s = " ".join(out)
    return s if len(s) < 600 else s[:s.rfind(" ", 0, 600)] + " ..."


def entry(path):
    g = Graph()
    g.parse(path, format="turtle")
    text = path.read_text(encoding="utf-8")
    circuit = next(iter(g.subjects(RDF.type, VAL.Circuit)), None)
    classes = sorted({local(o) for s, o in g.subject_objects(RDF.type)
                      if str(o).startswith(str(VAL)) and local(o) not in NOT_ELEMENTS})
    role = next((local(o) for o in g.objects(circuit, TRN.role)), "") if circuit else ""
    label = str(next(g.objects(circuit, RDFS.label), "")) if circuit else ""
    voices = next((str(o) for o in g.objects(circuit, VAL.voices)), "") if circuit else ""
    kind = ROLES.get(role)
    if not kind:  # no profile: a circuit played from MIDI is an instrument, anything else is taken to be an effect
        kind = "Instrument" if {"MidiPitch", "NoteGate", "MidiCC"} & set(classes) else "Effect"
    return {
        "id": path.stem,
        "title": label or path.stem,
        "kind": kind,
        "genres": sorted(str(o) for o in g.objects(circuit, TRN.genre)) if circuit else [],
        "input": sorted(local(o) for o in g.objects(circuit, TRN.accepts)) if circuit else [],
        "voices": int(voices) if voices.isdigit() else 0,
        "params": len(set(g.subjects(RDF.type, VAL.Param))),
        "elements": classes,
        "about": summary(text),
        "lines": text.count("\n") + 1,
        "url": REPO + path.name,
    }


PAGE = r"""<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Circuit catalogue — Valis</title><link rel="stylesheet" href="style.css">
<style>
body{max-width:62rem}
#q{width:100%;padding:.6rem .8rem;font:inherit;border:1px solid #888;border-radius:6px;box-sizing:border-box}
.facets{display:flex;flex-wrap:wrap;gap:.4rem;margin:.8rem 0}
.chip{border:1px solid #888;border-radius:99px;padding:.15rem .7rem;background:none;color:inherit;font:inherit;font-size:.85rem;cursor:pointer}
.chip[aria-pressed=true]{background:#3b6fd4;border-color:#3b6fd4;color:#fff}
.group{margin:.6rem 0 0;font-size:.8rem;text-transform:uppercase;letter-spacing:.06em;opacity:.7}
#count{margin:.5rem 0;opacity:.75}
.card{border:1px solid #8885;border-radius:8px;padding:.7rem 1rem;margin:.6rem 0}
.card h3{margin:0 0 .2rem;font-size:1.05rem}
.card p{margin:.3rem 0}
.meta{font-size:.82rem;opacity:.75}
.tag{display:inline-block;margin:.1rem .25rem .1rem 0;padding:0 .5rem;border-radius:4px;background:#8882;font-size:.78rem;cursor:pointer}
mark{background:#f5d76e;color:#000}
</style></head><body>
<nav><a href="index.html">Valis</a> · <a href="circuits.html">Circuits</a> · <a href="catalogue.html">Circuit catalogue</a>
 · <a href="element-catalogue.html">Element catalogue</a> · <a href="elements.html">Elements</a> · <a href="mcp.html">MCP</a> · <a href="https://github.com/danja/valis">GitHub</a></nav>
<h1>Circuit catalogue</h1>
<p>Every circuit in <code>circuits/</code>. Type to search names, descriptions, genres and the elements a circuit uses
(try <code>Glottal</code> or <code>Ladder</code>). The <a href="element-catalogue.html">element catalogue</a> is the same idea for the parts themselves. Choose a kind, genre or element to narrow the list. Each element links to its entry in the
<a href="elements.html">element reference</a>.</p>
<input id="q" type="search" placeholder="Search circuits, for example: bell, drums, val:Ladder, dub" autofocus>
<div id="kinds" class="facets"></div><div id="genres" class="facets"></div>
<div id="count" aria-live="polite"></div><div id="list"></div>
<script>
const DATA = __DATA__;
const $ = id => document.getElementById(id);
const state = {q:"", kind:"", genre:"", element:""};
const esc = s => s.replace(/[&<>"]/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c]));
const hay = d => (d.id+" "+d.title+" "+d.kind+" "+d.genres.join(" ")+" "+d.elements.join(" ")+" "+d.about).toLowerCase();
DATA.forEach(d => d.h = hay(d));
function chips(el, items, key) {
  el.innerHTML = items.map(([v,n]) => `<button class="chip" data-k="${key}" data-v="${esc(v)}" aria-pressed="${state[key]===v}">${esc(v)} ${n}</button>`).join("");
}
function counts(list, f) { const m = {}; list.forEach(d => f(d).forEach(v => m[v] = (m[v]||0)+1)); return Object.entries(m).sort((a,b)=>b[1]-a[1]||a[0].localeCompare(b[0])); }
function matches(d, skip) {
  const words = state.q.toLowerCase().replace(/val:/g,"").split(/\s+/).filter(Boolean);
  return words.every(w => d.h.includes(w))
    && (skip==="kind" || !state.kind || d.kind===state.kind)
    && (skip==="genre" || !state.genre || d.genres.includes(state.genre))
    && (!state.element || d.elements.includes(state.element));
}
function render() {
  const shown = DATA.filter(d => matches(d));
  chips($("kinds"), counts(DATA.filter(d=>matches(d,"kind")), d=>[d.kind]), "kind");
  chips($("genres"), counts(DATA.filter(d=>matches(d,"genre")), d=>d.genres).slice(0,24), "genre");
  $("count").textContent = shown.length + " of " + DATA.length + " circuits" + (state.element ? " using val:"+state.element : "");
  $("list").innerHTML = shown.map(d => `<div class="card" id="${d.id}">
    <h3><a href="${d.url}">${esc(d.title)}</a> <span class="meta">${d.id}.ttl</span></h3>
    <div class="meta">${d.kind}${d.input.length?" · plays from "+d.input.join(", "):""}${d.voices?" · "+d.voices+" voices":""} · ${d.params} controls · ${d.lines} lines</div>
    <p>${esc(d.about)}</p>
    <div>${d.genres.map(g=>`<span class="tag" data-k="genre" data-v="${esc(g)}">${esc(g)}</span>`).join("")}
    ${d.elements.map(e=>`<span class="tag" data-k="element" data-v="${e}">${e}</span>`).join("")}</div></div>`).join("");
}
document.addEventListener("click", e => {
  const t = e.target.closest("[data-k]"); if (!t) return;
  const k = t.dataset.k, v = t.dataset.v;
  state[k] = state[k]===v ? "" : v; render(); scrollTo(0,0);
});
$("q").addEventListener("input", e => { state.q = e.target.value; render(); });
const hash = decodeURIComponent(location.hash.slice(1));
if (hash) { state.q = ""; }
render();
if (hash && $(hash)) $(hash).scrollIntoView();
</script></body></html>
"""


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "_site")
    out.mkdir(parents=True, exist_ok=True)
    data = [entry(p) for p in sorted((ROOT / "circuits").glob("*.ttl"))]
    (out / "catalogue.json").write_text(json.dumps(data, indent=1), encoding="utf-8")
    page = PAGE.replace("__DATA__", json.dumps(data).replace("</", "<\\/"))
    (out / "catalogue.html").write_text(page, encoding="utf-8")
    catalogue_elements.write(out, data)
    print(f"catalogue: {len(data)} circuits, {sum(1 for d in data if not d['genres'])} without genres")


if __name__ == "__main__":
    main()
