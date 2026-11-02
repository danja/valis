// tests/engine/MidiOutTest.cpp
//
// Rendering offline with a val:NoteOut in the circuit: the notes it sends come back from renderBlocks located by stream
// position, and writeMidiScore turns them into a standard MIDI file that readMidiScore reads back at the same places.
// A note-on at sample 1000 is not on a 512-sample block boundary, which is the case that has gone wrong before.

#include "valis/CircuitCompiler.h"
#include "valis/CircuitModel.h"
#include "valis/DspElement.h"
#include "valis/MidiScore.h"
#include "valis/OfflineRender.h"
#include "valis/Ontology.h"
#include "valis/TurtleStore.h"
#include "valis/ValisEngine.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace valis;

namespace {

const Ontology& ontology()
{
    static const Ontology loaded = [] {
        Ontology o;
        std::vector<std::string> errors;
        o.loadUnits(VALIS_VOCABS_DIR "/lv2/units.ttl", errors);
        const bool ok = o.loadFile(VALIS_VOCABS_DIR "/valis.ttl", errors);
        assert(ok);
        return o;
    }();
    return loaded;
}

constexpr const char* kCircuit = R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix :    <urn:valis:t#> .
:c a val:Circuit ; val:element :gate , :pitch , :note , :out ; val:arc :m1 , :m2 .
:gate a val:NoteGate ; val:note 60.0 .
:pitch a val:MidiPitch .
:note a val:NoteOut ; val:velocity 0.5 ; val:channel 3.0 .
:out a val:Output .
:m1 a val:Arc ; val:from [ val:node :gate ; val:port "gate"  ] ; val:to [ val:node :note ; val:port "gate"  ] .
:m2 a val:Arc ; val:from [ val:node :pitch ; val:port "out" ] ; val:to [ val:node :note ; val:port "pitch" ] .
)";

}  // namespace

int main()
{
    rdf::TurtleStore store;
    std::vector<rdf::ParseError> parseErrors;
    const bool parsed = store.parse(kCircuit, "urn:valis:test", parseErrors);
    assert(parsed);
    CircuitModel model;
    std::vector<Diagnostic> diagnostics;
    const bool built = model.build(store, ontology(), diagnostics);
    assert(built);
    CompiledCircuit circuit;
    CircuitCompiler compiler;
    const bool compiled = compiler.compile(model, ontology(), circuit, diagnostics);
    assert(compiled);

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(48000.0, 512);
    std::string error;
    const bool loaded = engine.load(circuit, registry, error);
    assert(loaded);

    // A note held from sample 1000 to 6000, rendered over 8000 samples in blocks of 512.
    std::vector<TimedNote> score;
    score.push_back({1000, true, 60, 1.0f});
    score.push_back({6000, false, 60, 0.0f});
    BlockRun run;
    run.blockSize = 512;
    StereoAudio audio;
    std::vector<TimedNote> sent;
    renderBlocks(engine, std::vector<float>(8000, 0.0f), score, run, audio, &sent);

    // One note on and one note off, each in the block that contains it, to within a control slice (32 samples).
    assert(sent.size() == 2);
    assert(sent[0].on && sent[0].note == 60 && sent[0].channel == 3);
    assert(sent[0].sample >= 1000 && sent[0].sample < 1032);
    assert(! sent[1].on && sent[1].note == 60 && sent[1].channel == 3);
    assert(sent[1].sample >= 6000 && sent[1].sample < 6032);

    // Not asked for, nothing is collected and nothing changes.
    const bool reloaded = engine.load(circuit, registry, error);
    assert(reloaded);
    StereoAudio quiet;
    renderBlocks(engine, std::vector<float>(8000, 0.0f), score, run, quiet);
    assert(quiet.left == audio.left);

    // The file reads back with the same notes at the same places (a tick is 50 samples at 120 bpm and 48 kHz).
    const std::string path = "midi-out-test.mid";
    std::string fileError;
    const bool written = writeMidiScore(path, sent, 120.0, 48000.0, fileError);
    assert(written);
    std::vector<TimedNote> back;
    const bool read = readMidiScore(path, 120.0, 48000.0, back, fileError);
    assert(read);
    assert(back.size() == 2);
    assert(back[0].on && back[0].note == 60 && std::abs(back[0].sample - sent[0].sample) <= 50);
    assert(! back[1].on && std::abs(back[1].sample - sent[1].sample) <= 50);
    assert(std::abs(back[0].velocity - 0.5f) < 0.01f);
    std::remove(path.c_str());

    // A controller change survives the round trip too, and an empty list is a valid, empty file.
    TimedNote cc;
    cc.sample = 24000;
    cc.controller = 2;
    cc.velocity = 1.0f;
    const bool wroteCc = writeMidiScore(path, {cc}, 120.0, 48000.0, fileError);
    assert(wroteCc);
    std::vector<TimedNote> ccBack;
    const bool readCc = readMidiScore(path, 120.0, 48000.0, ccBack, fileError, true);
    assert(readCc && ccBack.size() == 1 && ccBack[0].controller == 2 && std::abs(ccBack[0].sample - 24000) <= 50);
    const bool wroteNone = writeMidiScore(path, {}, 120.0, 48000.0, fileError);
    assert(wroteNone);
    std::remove(path.c_str());

    // Failure is reported, not silent.
    assert(! writeMidiScore("/nonexistent-dir/x.mid", sent, 120.0, 48000.0, fileError));
    assert(! fileError.empty());
    assert(! writeMidiScore(path, sent, 0.0, 48000.0, fileError));

    std::puts("MidiOutTest passed");
    return 0;
}
