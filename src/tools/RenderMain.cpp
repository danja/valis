// src/tools/RenderMain.cpp
//
// valis-render - runs a circuit offline and writes a wav.
//
// No GUI, no host, no audio device: the fastest way to hear a change, and the
// harness the golden-output tests use.

#include "valis/CircuitCompiler.h"
#include "valis/CircuitModel.h"
#include "valis/DspElement.h"
#include "valis/Ontology.h"
#include "valis/TurtleStore.h"
#include "valis/ValisEngine.h"
#include "valis/Vocabulary.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Options
{
    std::string circuitPath;
    std::string outputPath = "out.wav";
    std::string inputPath;
    std::string vocabsPath = VALIS_VOCABS_DIR "/valis.ttl";
    double sampleRate = 48000.0;
    double seconds    = 2.0;
    int    blockSize  = 512;
    double toneHz     = 0.0;   ///< synthesise a test tone as the input
    bool   listElements = false;
    int    midiNote   = -1;    ///< -1 means no MIDI; otherwise note-on fires at gate-on time
    float  velocity   = 1.0f;
    double gateOnSec  = 0.0;
    double gateOffSec = -1.0;  ///< -1 means 80 % of seconds
};

/// Prints the element reference as markdown, straight from the ontology, so
/// docs/manual/elements.md cannot drift from what the plugin actually offers.
void printElementReference(const valis::Ontology& ontology)
{
    std::printf("# Element reference\n\n");
    std::printf("Generated from `vocabs/valis.ttl` by `valis-render --elements`.\n");
    std::printf("Do not edit by hand; run `scripts/generate-docs.sh` instead.\n\n");
    std::printf("The Filter/Transfer split is memory versus no memory. Linearity is a\n");
    std::printf("separate property: a ladder filter has memory and is nonlinear.\n\n");

    for (const auto* type : ontology.types())
    {
        const auto name = valis::vocab::shortName(type->classIri);
        std::printf("## val:%s\n\n", name.c_str());

        if (! type->label.empty() && type->label != name)
            std::printf("%s. ", type->label.c_str());
        std::printf("%s.\n\n", type->linear ? "Linear" : "Nonlinear");

        const auto ports = [&](bool input, bool control, const char* heading)
        {
            const auto matching = type->portsMatching(input, control);
            if (matching.empty())
                return;

            std::printf("**%s**", heading);
            if (control && input)
            {
                std::printf("\n\n| symbol | name | default | range | unit |\n");
                std::printf("|---|---|---|---|---|\n");
                for (const auto* port : matching)
                    std::printf("| `%s` | %s | %g | %g to %g | %s |\n",
                                port->symbol.c_str(), port->name.c_str(),
                                port->defaultValue, port->minimum, port->maximum,
                                port->unitSymbol.empty() ? "" : port->unitSymbol.c_str());
                std::printf("\n");
            }
            else
            {
                std::printf(": ");
                for (std::size_t i = 0; i < matching.size(); ++i)
                    std::printf("%s`%s`", i ? ", " : "", matching[i]->symbol.c_str());
                std::printf("\n\n");
            }
        };

        ports(true,  false, "Audio in");
        ports(false, false, "Audio out");
        ports(false, true,  "Control out");
        ports(true,  true,  "Controls");
    }
}

void usage()
{
    std::puts(
        "valis-render - render a Valis circuit to a wav file\n"
        "\n"
        "  valis-render <circuit.ttl> [options]\n"
        "\n"
        "  -o <file>        output wav (default out.wav)\n"
        "  -i <file>        input wav; otherwise the input is silence\n"
        "  --tone <hz>      synthesise a sine as the input instead\n"
        "  --seconds <s>    duration when there is no input file (default 2)\n"
        "  --rate <hz>      sample rate (default 48000)\n"
        "  --block <n>      block size (default 512)\n"
        "  --vocabs <file>  ontology to load (default the shipped vocabs/valis.ttl)\n"
        "  --elements       print the element reference as markdown and exit\n"
        "\n"
        "MIDI (for circuits driven by note events):\n"
        "  --note <n>       send a note-on for MIDI note n (0-127) at --gate-on time\n"
        "  --velocity <v>   note velocity 0.0-1.0 (default 1.0)\n"
        "  --gate-on <s>    time in seconds when the note fires (default 0.0)\n"
        "  --gate-off <s>   time in seconds when the note is released (default 80% of --seconds)");
}

bool parse(int argc, char** argv, Options& options)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string{}; };

        if (arg == "-h" || arg == "--help")      { usage(); return false; }
        else if (arg == "-o")                    options.outputPath = next();
        else if (arg == "-i")                    options.inputPath  = next();
        else if (arg == "--tone")                options.toneHz     = std::stod(next());
        else if (arg == "--seconds")             options.seconds    = std::stod(next());
        else if (arg == "--rate")                options.sampleRate = std::stod(next());
        else if (arg == "--block")               options.blockSize  = std::stoi(next());
        else if (arg == "--vocabs")              options.vocabsPath = next();
        else if (arg == "--elements")            options.listElements = true;
        else if (arg == "--note")                options.midiNote   = std::stoi(next());
        else if (arg == "--velocity")            options.velocity   = std::stof(next());
        else if (arg == "--gate-on")             options.gateOnSec  = std::stod(next());
        else if (arg == "--gate-off")            options.gateOffSec = std::stod(next());
        else if (! arg.empty() && arg[0] == '-') { std::fprintf(stderr, "unknown option %s\n", arg.c_str()); return false; }
        else                                     options.circuitPath = arg;
    }

    if (options.circuitPath.empty() && ! options.listElements)
    {
        usage();
        return false;
    }
    return true;
}

std::vector<float> readInput(const Options& options)
{
    const auto totalSamples = static_cast<int>(options.sampleRate * options.seconds);

    if (! options.inputPath.empty())
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader(
            formats.createReaderFor(juce::File(options.inputPath)));

        if (reader == nullptr)
        {
            std::fprintf(stderr, "cannot read %s\n", options.inputPath.c_str());
            return {};
        }

        juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels),
                                        static_cast<int>(reader->lengthInSamples));
        reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);

        // Mono for now: the circuit model has one audio channel per port.
        std::vector<float> mono(static_cast<std::size_t>(buffer.getNumSamples()), 0.0f);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float sum = 0.0f;
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                sum += buffer.getSample(c, i);
            mono[static_cast<std::size_t>(i)] = sum / static_cast<float>(buffer.getNumChannels());
        }
        return mono;
    }

    std::vector<float> input(static_cast<std::size_t>(totalSamples), 0.0f);
    if (options.toneHz > 0.0)
        for (int i = 0; i < totalSamples; ++i)
            input[static_cast<std::size_t>(i)] = 0.5f * static_cast<float>(
                std::sin(2.0 * 3.14159265358979 * options.toneHz * i / options.sampleRate));

    return input;
}

}  // namespace

int main(int argc, char** argv)
{
    Options options;
    if (! parse(argc, argv, options))
        return 1;

    // -- ontology ----------------------------------------------------------
    valis::Ontology ontology;
    std::vector<std::string> ontologyErrors;

    // Units first: symbols are resolved as ports are read.
    ontology.loadUnits(VALIS_VOCABS_DIR "/lv2/units.ttl", ontologyErrors);

    if (! ontology.loadFile(options.vocabsPath, ontologyErrors))
    {
        for (const auto& e : ontologyErrors)
            std::fprintf(stderr, "ontology: %s\n", e.c_str());
        return 1;
    }

    if (options.listElements)
    {
        printElementReference(ontology);
        return 0;
    }

    // -- circuit -----------------------------------------------------------
    valis::rdf::TurtleStore store;
    std::vector<valis::rdf::ParseError> parseErrors;
    if (! store.parseFile(options.circuitPath, parseErrors))
    {
        for (const auto& e : parseErrors)
            std::fprintf(stderr, "%s:%s\n", options.circuitPath.c_str(), e.toString().c_str());
        return 1;
    }

    valis::CircuitModel model;
    std::vector<valis::Diagnostic> diagnostics;
    if (! model.build(store, ontology, diagnostics))
    {
        for (const auto& d : diagnostics)
            std::fprintf(stderr, "%s\n", d.toString().c_str());
        return 1;
    }

    valis::CompiledCircuit compiled;
    valis::CircuitCompiler compiler;
    if (! compiler.compile(model, ontology, compiled, diagnostics))
    {
        for (const auto& d : diagnostics)
            std::fprintf(stderr, "%s\n", d.toString().c_str());
        return 1;
    }

    for (const auto& d : diagnostics)
        std::fprintf(stderr, "warning: %s\n", d.toString().c_str());

    // -- render ------------------------------------------------------------
    const auto registry = valis::makeDefaultRegistry();
    valis::ValisEngine engine;
    engine.prepare(options.sampleRate, options.blockSize);

    std::string error;
    if (! engine.load(compiled, registry, error))
    {
        std::fprintf(stderr, "engine: %s\n", error.c_str());
        return 1;
    }

    const auto input = readInput(options);
    if (input.empty())
        return 1;

    std::vector<float> outputL(input.size(), 0.0f);
    std::vector<float> outputR(input.size(), 0.0f);

    const int noteOnSample = options.midiNote >= 0
        ? static_cast<int>(options.gateOnSec * options.sampleRate)
        : -1;
    const double gateOff = options.gateOffSec >= 0.0
        ? options.gateOffSec
        : options.seconds * 0.8;
    const int noteOffSample = options.midiNote >= 0
        ? static_cast<int>(gateOff * options.sampleRate)
        : -1;

    for (std::size_t at = 0; at < input.size(); at += static_cast<std::size_t>(options.blockSize))
    {
        const auto sample = static_cast<int>(at);
        if (options.midiNote >= 0)
        {
            if (sample == noteOnSample)
                engine.noteOn(options.midiNote, options.velocity);
            if (noteOffSample > noteOnSample && sample == noteOffSample)
                engine.noteOff(options.midiNote);
        }

        const auto n = static_cast<int>(std::min(static_cast<std::size_t>(options.blockSize),
                                                 input.size() - at));
        engine.process(input.data() + at, outputL.data() + at, outputR.data() + at, n);
    }

    // -- write -------------------------------------------------------------
    juce::File outputFile(juce::File::getCurrentWorkingDirectory()
                              .getChildFile(juce::String(options.outputPath)));
    outputFile.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream(outputFile.createOutputStream());
    if (stream == nullptr)
    {
        std::fprintf(stderr, "cannot write %s\n", options.outputPath.c_str());
        return 1;
    }

    auto writerOptions = juce::AudioFormatWriterOptions()
                             .withSampleRate(options.sampleRate)
                             .withNumChannels(2)
                             .withBitsPerSample(24);
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(stream, writerOptions));
    if (writer == nullptr)
    {
        std::fprintf(stderr, "cannot create wav writer\n");
        return 1;
    }

    const float* channels[] = {outputL.data(), outputR.data()};
    writer->writeFromFloatArrays(channels, 2, static_cast<int>(outputL.size()));
    writer.reset();

    float peak = 0.0f;
    double sum = 0.0;
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        const float s = 0.5f * (outputL[i] + outputR[i]);
        peak = std::max(peak, std::abs(s));
        sum += static_cast<double>(s) * s;
    }
    const auto rms = std::sqrt(sum / static_cast<double>(outputL.size()));

    std::printf("%s: %zu nodes, %d buffers, latency %d\n",
                options.circuitPath.c_str(), compiled.nodes.size(),
                compiled.numBuffers, engine.latencyInSamples());
    std::printf("%s: %zu samples, peak %.4f, rms %.4f\n",
                options.outputPath.c_str(), outputL.size(), peak, rms);

    if (peak > 1.0f)
        std::fprintf(stderr,
                     "warning: peak %.2f exceeds full scale; the wav is clipped. "
                     "The circuit needs gain staging.\n", peak);

    if (! std::isfinite(peak))
    {
        std::fprintf(stderr, "error: output is not finite; the circuit diverged\n");
        return 1;
    }

    return 0;
}
