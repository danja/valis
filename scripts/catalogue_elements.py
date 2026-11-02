#!/usr/bin/env python3
# scripts/catalogue_elements.py
#
# Writes element-catalogue.html and element-catalogue.json: every element in vocabs/valis.ttl, grouped by
# what it does, with its ports and the circuits that use it. Run by scripts/catalogue.py. Needs rdflib.

import json, re
from pathlib import Path
from rdflib import Graph, Namespace, RDF, RDFS

VAL = Namespace("http://purl.org/stuff/valis/")
LV2 = Namespace("http://lv2plug.in/ns/lv2core#")
UNITS = Namespace("http://lv2plug.in/ns/extensions/units#")
ROOT = Path(__file__).resolve().parent.parent

# The ontology only separates Source, Filter and Transfer, which is too coarse to browse by. This table is the
# grouping; the script stops when an element is missing from it or named here and absent from the ontology.
GROUPS = {
    "Oscillators and noise": "Oscillator Wavetable Noise SignalGenerator Glottal FMOperator FMMatrix Drift TwinTBridge",
    "Physical models": "Bow Brass Flute Reed Pluck Shaker StiffString ModalBank FreeModalBank",
    "Filters": "Ladder StateVariable Biquad OnePole CombFilter Crossover ToneStack Vocoder LPC",
    "Distortion and shaping": "Tanh SoftSine SinArcTan HardClip AsymClip Diode DiodePair Fold Triode Tape Redux",
    "Dynamics": "Compressor Limiter Expander EnvelopeFollower",
    "Envelopes and modulation": "Envelope RateLevelEnvelope LFO Slew Vary SampleHold StepSequencer",
    "Delay and reverb": "Delay UnitDelay BucketBrigade Reverb Convolver Chorus Flanger Phaser",
    "Pitch and spectrum": "PitchShift FreqShifter OctaveDivider PitchTracker TimeStretch SpectralGate SpectralDelay Codec RingModulator",
    "Samples and grains": "Sampler SampleLoad GrainBuffer GrainCloud Granulator NamModel",
    "MIDI and transport": "MidiPitch MidiVelocity MidiCC NoteGate NoteOut MidiInterval Transport Quantise",
    "Mixing and routing": "Gain VCA Pan Mixer DryWet Select AudioSelect Choke Input Output",
    "Control maths": "Scale ControlAdd ControlMultiply ControlMax",
    "Monitors": "Oscilloscope FreqAnalyzer",
}
GROUP_OF = {n: g for g, names in GROUPS.items() for n in names.split()}


def local(iri):
    return re.split(r"[#/]", str(iri).rstrip("/"))[-1]


def kind_of(g, port):
    types = {local(t) for t in g.objects(port, RDF.type)}
    return "buffer" if "BufferPort" in types else "audio" if "AudioPort" in types else "control" \
        if "ControlPort" in types else "other"


def elements(circuits):
    g = Graph()
    g.parse(ROOT / "vocabs" / "valis.ttl", format="turtle")
    used = {}
    for c in circuits:
        for e in c["elements"]:
            used.setdefault(e, []).append(c["id"])
    out, names = [], set()
    for s in g.subjects(VAL.implementation, None):
        name = local(s)
        names.add(name)
        ports = []
        for p in g.objects(s, LV2.port):
            d = lambda prop: next((str(o) for o in g.objects(p, prop)), "")
            ports.append({"symbol": d(LV2.symbol), "name": d(LV2.name), "dir": "in" if (p, RDF.type, LV2.InputPort) in g else "out",
                          "kind": kind_of(g, p), "default": d(LV2.default), "min": d(LV2.minimum), "max": d(LV2.maximum),
                          "unit": local(d(UNITS.unit)) if d(UNITS.unit) else ""})
        flow = ("audio processor" if any(p["kind"] == "audio" and p["dir"] == "in" for p in ports) and
                any(p["kind"] == "audio" and p["dir"] == "out" for p in ports) else
                "audio source" if any(p["kind"] == "audio" and p["dir"] == "out" for p in ports) else
                "audio sink" if any(p["kind"] == "audio" for p in ports) else "control only")
        out.append({"id": name, "group": GROUP_OF.get(name, ""), "flow": flow,
                    "about": str(next(g.objects(s, RDFS.comment), "")).strip(),
                    "linear": next((str(o).lower() == "true" for o in g.objects(s, VAL.linear)), None),
                    "ports": ports, "circuits": sorted(used.get(name, []))})
    missing, stale = sorted(names - set(GROUP_OF)), sorted(set(GROUP_OF) - names)
    if missing or stale:
        raise SystemExit(f"scripts/catalogue_elements.py: add to GROUPS: {missing}; remove from GROUPS: {stale}")
    return sorted(out, key=lambda e: (e["group"], e["id"]))


PAGE = r"""<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Element catalogue — Valis</title><link rel="stylesheet" href="style.css">
<style>
body{max-width:62rem}
#q{width:100%;padding:.6rem .8rem;font:inherit;border:1px solid #888;border-radius:6px;box-sizing:border-box}
.facets{display:flex;flex-wrap:wrap;gap:.4rem;margin:.8rem 0}
.chip{border:1px solid #888;border-radius:99px;padding:.15rem .7rem;background:none;color:inherit;font:inherit;font-size:.85rem;cursor:pointer}
.chip[aria-pressed=true]{background:#3b6fd4;border-color:#3b6fd4;color:#fff}
#count{margin:.5rem 0;opacity:.75}
h2.g{margin:1.6rem 0 .2rem;font-size:1.1rem}
.card{border:1px solid #8885;border-radius:8px;padding:.7rem 1rem;margin:.6rem 0}
.card h3{margin:0 0 .2rem;font-size:1.05rem}
.card p{margin:.3rem 0}
.meta{font-size:.82rem;opacity:.75}
.tag{display:inline-block;margin:.1rem .25rem .1rem 0;padding:0 .5rem;border-radius:4px;background:#8882;font-size:.78rem}
a.tag{text-decoration:none}
details{margin:.3rem 0}summary{cursor:pointer;font-size:.85rem}
table{font-size:.82rem}
</style></head><body>
<nav><a href="index.html">Valis</a> · <a href="circuits.html">Circuits</a> · <a href="catalogue.html">Circuit catalogue</a>
 · <a href="element-catalogue.html">Element catalogue</a> · <a href="elements.html">Elements</a> · <a href="mcp.html">MCP</a>
 · <a href="https://github.com/danja/valis">GitHub</a></nav>
<h1>Element catalogue</h1>
<p>Every element a circuit can use, grouped by what it does. Search by name, purpose or port (try <code>cutoff</code> or
<code>envelope</code>). Each card lists the circuits that use the element, which are good examples of it in use. The full port tables are in the
<a href="elements.html">element reference</a>.</p>
<input id="q" type="search" placeholder="Search elements, for example: reverb, filter, cutoff, grain" autofocus>
<div id="groups" class="facets"></div><div id="flows" class="facets"></div>
<div id="count" aria-live="polite"></div><div id="list"></div>
<script>
const DATA = __DATA__;
const $ = id => document.getElementById(id);
const state = {q:"", group:"", flow:""};
const esc = s => String(s).replace(/[&<>"]/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c]));
DATA.forEach(d => d.h = (d.id+" "+d.group+" "+d.flow+" "+d.about+" "+d.ports.map(p=>p.symbol+" "+p.name).join(" ")).toLowerCase());
function counts(list, f) { const m = {}; list.forEach(d => { m[f(d)] = (m[f(d)]||0)+1; }); return Object.entries(m); }
function chips(el, items, key) {
  el.innerHTML = items.map(([v,n]) => `<button class="chip" data-k="${key}" data-v="${esc(v)}" aria-pressed="${state[key]===v}">${esc(v)} ${n}</button>`).join("");
}
function matches(d, skip) {
  const words = state.q.toLowerCase().replace(/val:/g,"").split(/\s+/).filter(Boolean);
  return words.every(w => d.h.includes(w)) && (skip==="group"||!state.group||d.group===state.group) && (skip==="flow"||!state.flow||d.flow===state.flow);
}
function ports(d) {
  const rows = d.ports.map(p => `<tr><td><code>${esc(p.symbol)}</code></td><td>${p.dir} ${p.kind}</td><td>${esc(p.name)}</td><td>${esc(p.default)}</td><td>${p.min!==""?esc(p.min)+" to "+esc(p.max):""} ${esc(p.unit)}</td></tr>`).join("");
  return `<details><summary>${d.ports.length} ports</summary><table><tr><th>symbol</th><th>kind</th><th>name</th><th>default</th><th>range</th></tr>${rows}</table></details>`;
}
function render() {
  const shown = DATA.filter(d => matches(d));
  chips($("groups"), counts(DATA.filter(d=>matches(d,"group")), d=>d.group), "group");
  chips($("flows"), counts(DATA.filter(d=>matches(d,"flow")), d=>d.flow), "flow");
  $("count").textContent = shown.length + " of " + DATA.length + " elements";
  let last = "", html = "";
  for (const d of shown) {
    if (d.group !== last) { html += `<h2 class="g">${esc(d.group)}</h2>`; last = d.group; }
    html += `<div class="card" id="${d.id}"><h3>val:${d.id}</h3>
      <div class="meta">${d.flow}${d.linear===true?" · linear":d.linear===false?" · nonlinear":""} · ${d.circuits.length} circuits use it</div>
      <p>${esc(d.about)}</p>${ports(d)}
      <div>${d.circuits.slice(0,12).map(c=>`<a class="tag" href="catalogue.html#${c}">${c}</a>`).join("")}${d.circuits.length>12?`<span class="tag">+${d.circuits.length-12} more</span>`:""}</div></div>`;
  }
  $("list").innerHTML = html;
}
document.addEventListener("click", e => {
  const t = e.target.closest("[data-k]"); if (!t) return;
  state[t.dataset.k] = state[t.dataset.k]===t.dataset.v ? "" : t.dataset.v; render();
});
$("q").addEventListener("input", e => { state.q = e.target.value; render(); });
render();
const hash = decodeURIComponent(location.hash.slice(1));
if (hash && $(hash)) $(hash).scrollIntoView();
</script></body></html>
"""


def write(out, circuits):
    data = elements(circuits)
    (out / "element-catalogue.json").write_text(json.dumps(data, indent=1), encoding="utf-8")
    (out / "element-catalogue.html").write_text(PAGE.replace("__DATA__", json.dumps(data).replace("</", "<\\/")), encoding="utf-8")
    print(f"element catalogue: {len(data)} elements in {len(GROUPS)} groups, {sum(1 for d in data if not d['circuits'])} used by no circuit")
