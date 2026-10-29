// tests/engine/GrainSplitTest.cpp
//
// val:GrainBuffer and val:GrainCloud, the two halves of val:Granulator joined by a
// buffer arc. The pair has to sound like the single element, several clouds have to be
// able to share one buffer, and the compiler has to refuse a buffer arc that makes no
// sense and put the buffer ahead of what reads it.

#include "valis/CircuitCompiler.h"
#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"
#include "valis/Ontology.h"
#include "valis/TurtleStore.h"
#include "valis/ValisEngine.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;
constexpr int    kBlock = 512;

void configure(ElementTestFixture& rig)
{
    rig.set("density", 40.0f);
    rig.set("size", 80.0f);
    rig.set("spray", 0.3f);
    rig.set("pitch", 3.0f);
    rig.set("spread", 0.8f);
    rig.set("jitter", 0.2f);
    rig.set("seed", 7.0f);
    rig.set("position", 0.2f);
}

std::vector<float> run(ElementTestFixture& rig, int blocks, std::vector<ElementTestFixture*> before = {})
{
    std::vector<float> out;
    const std::vector<float> silence(kBlock, 0.0f);
    for (int b = 0; b < blocks; ++b)
    {
        for (auto* other : before)
            other->run(silence);
        const auto block = rig.run(silence, "left");
        out.insert(out.end(), block.begin(), block.end());
    }
    return out;
}

/// With a file as the material nothing is recording, so the pair and the single element
/// start every grain from the same state and must agree to the last sample.
void testPairMatchesTheSingleElementOnAFile()
{
    ElementTestFixture single("Granulator", kRate);
    ElementTestFixture buffer("GrainBuffer", kRate);
    ElementTestFixture cloud("GrainCloud", kRate);

    std::string error;
    for (auto* rig : { &single, &buffer })
    {
        const bool ok = rig->element->setOption("file", "samples/bell.wav", error);
        assert(ok);
        rig->set("freeze", 1.0f);   // hold the file; do not record the fixture's silence over it
    }
    configure(single);
    configure(cloud);

    const bool wired = cloud.element->bufferIn("buffer", buffer.element->bufferOut("buffer"), error);
    assert(wired);

    const auto a = run(single, 40);
    const auto b = run(cloud, 40, {&buffer});

    float peak = 0.0f;
    for (const float v : a) peak = std::max(peak, std::abs(v));
    assert(peak > 0.01f);   // grains are sounding, so this compares something

    assert(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        assert(a[i] == b[i]);
    std::printf("  single element and pair agree over %zu samples, peak %.3f\n", a.size(), peak);
}

void testSeveralCloudsShareOneBuffer()
{
    ElementTestFixture buffer("GrainBuffer", kRate);
    std::string error;
    const bool loaded = buffer.element->setOption("file", "samples/bell.wav", error);
    assert(loaded);
    buffer.set("freeze", 1.0f);

    ElementTestFixture up("GrainCloud", kRate), down("GrainCloud", kRate);
    configure(up);
    configure(down);
    up.set("pitch", 12.0f);
    down.set("pitch", -12.0f);
    for (auto* cloud : { &up, &down })
    {
        const bool wired = cloud->element->bufferIn("buffer", buffer.element->bufferOut("buffer"), error);
        assert(wired);
    }

    const auto a = run(up, 30, {&buffer});
    const auto b = run(down, 30);   // the buffer is held, so it needs no running in between

    float peakA = 0.0f, peakB = 0.0f;
    for (const float v : a) peakA = std::max(peakA, std::abs(v));
    for (const float v : b) peakB = std::max(peakB, std::abs(v));
    assert(peakA > 0.01f && peakB > 0.01f);
    assert(a != b);
}

void testALiveBufferIsGranulated()
{
    ElementTestFixture buffer("GrainBuffer", kRate);
    ElementTestFixture cloud("GrainCloud", kRate);
    std::string error;
    const bool wired = cloud.element->bufferIn("buffer", buffer.element->bufferOut("buffer"), error);
    assert(wired);

    cloud.set("position", 0.99f);
    cloud.set("density", 60.0f);
    cloud.set("size", 60.0f);

    // A second of a tone goes into the buffer; grains from the end of it must carry it.
    double energy = 0.0;
    int samples = 0;
    for (int b = 0; b < 94; ++b)
    {
        std::vector<float> tone(kBlock);
        for (int i = 0; i < kBlock; ++i)
            tone[static_cast<std::size_t>(i)] =
                0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 440.0 * (b * kBlock + i) / kRate));

        buffer.run(tone);
        const auto out = cloud.run(std::vector<float>(kBlock, 0.0f), "out");
        if (b > 30)
            for (const float v : out)
            {
                assert(std::isfinite(v));
                energy += static_cast<double>(v) * v;
                ++samples;
            }
    }
    const double rms = std::sqrt(energy / samples);
    std::printf("  live buffer, grains from the recent end: rms %.3f\n", rms);
    assert(rms > 0.02);
}

void testAnUnconnectedCloudIsSilentAndFinite()
{
    ElementTestFixture cloud("GrainCloud", kRate);
    for (int b = 0; b < 5; ++b)
    {
        const auto out = cloud.run(std::vector<float>(kBlock, 0.0f), "left");
        for (const float v : out)
            assert(v == 0.0f);
    }

    // And a buffer input refuses what is not a grain buffer, and a port it does not have.
    struct Other final : SharedBuffer {} other;
    std::string error;
    assert(! cloud.element->bufferIn("buffer", &other, error));
    assert(error.find("val:GrainBuffer") != std::string::npos);
    assert(! cloud.element->bufferIn("nonsense", &other, error));
}

// -- the compiler and the engine --------------------------------------------------------

struct Compiled
{
    bool ok = false;
    std::vector<Diagnostic> diagnostics;
    CompiledCircuit circuit;
};

Compiled compileText(const std::string& body)
{
    static Ontology ontology = [] {
        Ontology o;
        std::vector<std::string> errors;
        o.loadUnits(VALIS_VOCABS_DIR "/lv2/units.ttl", errors);
        const bool ok = o.loadFile(VALIS_VOCABS_DIR "/valis.ttl", errors);
        assert(ok);
        return o;
    }();

    const std::string text =
        "@prefix val: <http://purl.org/stuff/valis/> .\n"
        "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n"
        "@prefix : <urn:valis:split#> .\n" + body;

    Compiled result;
    rdf::TurtleStore store;
    std::vector<rdf::ParseError> parseErrors;
    const bool parsed = store.parse(text, "urn:valis:circuit", parseErrors);
    assert(parsed);

    CircuitModel model;
    if (! model.build(store, ontology, result.diagnostics))
        return result;

    CircuitCompiler compiler;
    result.ok = compiler.compile(model, ontology, result.circuit, result.diagnostics);
    return result;
}

bool said(const Compiled& c, const char* fragment)
{
    return std::any_of(c.diagnostics.begin(), c.diagnostics.end(),
                       [&](const Diagnostic& d) { return d.message.find(fragment) != std::string::npos; });
}

const char* kPair = R"(
:c a val:Circuit ; val:element :in , :buf , :cloud , :out ; val:arc :a1 , :a2 , :a3 .
:in a val:Input .
:buf a val:GrainBuffer .
:cloud a val:GrainCloud ; val:position 0.99 ; val:density 60.0 ; val:size 60.0 .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ; val:to [ val:node :buf ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :buf ; val:port "buffer" ] ; val:to [ val:node :cloud ; val:port "buffer" ] .
:a3 a val:Arc ; val:from [ val:node :cloud ; val:port "out" ] ; val:to [ val:node :out ; val:port "in" ] .
)";

void testTheCompilerOrdersAndRecordsABufferArc()
{
    const auto c = compileText(kPair);
    for (const auto& d : c.diagnostics) std::printf("  %s\n", d.message.c_str());
    assert(c.ok);

    assert(c.circuit.bufferLinks.size() == 1);
    const auto& link = c.circuit.bufferLinks.front();
    assert(c.circuit.nodes[static_cast<std::size_t>(link.from)].id.find("buf") != std::string::npos);
    assert(c.circuit.nodes[static_cast<std::size_t>(link.to)].id.find("cloud") != std::string::npos);
    assert(link.from < link.to);   // the buffer runs first

    // And a buffer port takes no audio buffer and no control slot: the buffer element
    // has one audio input and nothing to write.
    for (const auto& node : c.circuit.nodes)
        if (node.id.find("buf") != std::string::npos)
            assert(node.audioOutBuffers.empty());
}

void testTheCompilerRefusesBufferMistakes()
{
    // A buffer output into an audio input.
    auto bad = compileText(R"(
:c a val:Circuit ; val:element :buf , :out ; val:arc :a1 .
:buf a val:GrainBuffer .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :buf ; val:port "buffer" ] ; val:to [ val:node :out ; val:port "in" ] .
)");
    assert(! bad.ok && said(bad, "buffer output to a audio input"));

    // An audio output into a buffer input.
    bad = compileText(R"(
:c a val:Circuit ; val:element :in , :cloud ; val:arc :a1 .
:in a val:Input .
:cloud a val:GrainCloud .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ; val:to [ val:node :cloud ; val:port "buffer" ] .
)");
    assert(! bad.ok && said(bad, "audio output to a buffer input"));

    // Two sources into one buffer input.
    bad = compileText(R"(
:c a val:Circuit ; val:element :b1 , :b2 , :cloud ; val:arc :a1 , :a2 .
:b1 a val:GrainBuffer .
:b2 a val:GrainBuffer .
:cloud a val:GrainCloud .
:a1 a val:Arc ; val:from [ val:node :b1 ; val:port "buffer" ] ; val:to [ val:node :cloud ; val:port "buffer" ] .
:a2 a val:Arc ; val:from [ val:node :b2 ; val:port "buffer" ] ; val:to [ val:node :cloud ; val:port "buffer" ] .
)");
    assert(! bad.ok && said(bad, "takes one source"));

    // One buffer feeding several clouds is the point.
    const auto fan = compileText(R"(
:c a val:Circuit ; val:element :b , :c1 , :c2 , :c3 , :out ; val:arc :a1 , :a2 , :a3 .
:out a val:Output .
:b a val:GrainBuffer .
:c1 a val:GrainCloud .
:c2 a val:GrainCloud .
:c3 a val:GrainCloud .
:a1 a val:Arc ; val:from [ val:node :b ; val:port "buffer" ] ; val:to [ val:node :c1 ; val:port "buffer" ] .
:a2 a val:Arc ; val:from [ val:node :b ; val:port "buffer" ] ; val:to [ val:node :c2 ; val:port "buffer" ] .
:a3 a val:Arc ; val:from [ val:node :b ; val:port "buffer" ] ; val:to [ val:node :c3 ; val:port "buffer" ] .
)");
    for (const auto& d : fan.diagnostics) std::printf("  %s\n", d.message.c_str());
    assert(fan.ok && fan.circuit.bufferLinks.size() == 3);
}

void testTheEngineConnectsAndRuns()
{
    const auto c = compileText(kPair);
    assert(c.ok);

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(kRate, kBlock);
    std::string error;
    const bool installed = engine.load(c.circuit, registry, error);
    if (! installed) std::printf("  %s\n", error.c_str());
    assert(installed);

    std::vector<float> in(kBlock), left(kBlock), right(kBlock);
    double energy = 0.0;
    int samples = 0;
    for (int b = 0; b < 94; ++b)
    {
        for (int i = 0; i < kBlock; ++i)
            in[static_cast<std::size_t>(i)] =
                0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 440.0 * (b * kBlock + i) / kRate));
        engine.process(in.data(), left.data(), right.data(), kBlock);
        if (b > 30)
            for (const float v : left)
            {
                assert(std::isfinite(v));
                energy += static_cast<double>(v) * v;
                ++samples;
            }
    }
    const double rms = std::sqrt(energy / samples);
    std::printf("  engine: input through a buffer arc to a cloud, rms %.3f\n", rms);
    assert(rms > 0.02);
}

void testAnElementThatCannotTakeTheBufferFailsTheLoad()
{
    // A cloud's buffer input given something that is not grain material: only possible
    // from a hand-built compiled circuit, since the ontology has no other buffer output.
    auto c = compileText(kPair);
    assert(c.ok);

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(kRate, kBlock);

    // Point the link at the wrong port name on the sender.
    c.circuit.bufferLinks.front().fromPort = "nonsense";
    std::string error;
    assert(! engine.load(c.circuit, registry, error));
    assert(error.find("no buffer output") != std::string::npos);
}

}  // namespace

int main()
{
    testPairMatchesTheSingleElementOnAFile();
    testSeveralCloudsShareOneBuffer();
    testALiveBufferIsGranulated();
    testAnUnconnectedCloudIsSilentAndFinite();
    testTheCompilerOrdersAndRecordsABufferArc();
    testTheCompilerRefusesBufferMistakes();
    testTheEngineConnectsAndRuns();
    testAnElementThatCannotTakeTheBufferFailsTheLoad();

    std::puts("GrainSplitTest PASSED");
    return 0;
}
