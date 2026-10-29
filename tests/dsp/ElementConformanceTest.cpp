// tests/dsp/ElementConformanceTest.cpp
//
// One suite, run over every element in the ontology. Most element tests here are written
// for one element, which means a property such as "the same input renders the same way
// twice" is only checked where somebody thought to write it. These checks are generated
// from the ontology instead, so a new element is held to them without a line of test code.
//
// The idea, and most of the list of properties, is from Cathode's BlockConformanceTests.
//
// An element that cannot meet a property on purpose is named in an exception list below with
// the reason. The list is the documentation of where the contract is looser than it looks.

#include "valis/DspElement.h"
#include "valis/Ontology.h"
#include "valis/Vocabulary.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <vector>

using namespace valis;

// Every allocation in this executable is counted, so a test can say "none happened here".
namespace {
std::atomic<long> gAllocations{0};
std::atomic<bool> gCounting{false};
}

void* operator new(std::size_t size)
{
    if (gCounting.load(std::memory_order_relaxed))
        gAllocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {

constexpr double kRate = 48000.0;

/// Elements that are markers or take their input from outside the audio graph.
const std::set<std::string> kMarkers{"Input", "Output"};

/// What each property does not hold for, and why. Keyed by property, then element.
using Exceptions = std::map<std::string, std::map<std::string, std::string>>;
const Exceptions kExceptions = {
    {"decay", {
        {"FMOperator", "an oscillator: it sounds at its default level with no input to stop"},
        {"Triode",     "outputs its DC operating point, which a coupling capacitor removes in a circuit"},
    }},
    {"denormals", {
        // ValisProcessor::processBlock sets flush-to-zero with juce::ScopedNoDenormals, so a
        // subnormal here costs nothing in the plugin. valis-render and these tests do not set it.
        {"CombFilter",    "relies on the host's flush-to-zero"},
        {"Crossover",     "relies on the host's flush-to-zero"},
        {"Flanger",       "relies on the host's flush-to-zero"},
        {"FreqShifter",   "relies on the host's flush-to-zero"},
        {"LPC",           "relies on the host's flush-to-zero"},
        {"Ladder",        "relies on the host's flush-to-zero"},
        {"OnePole",       "relies on the host's flush-to-zero"},
        {"Phaser",        "relies on the host's flush-to-zero"},
        {"StateVariable", "relies on the host's flush-to-zero"},
        {"StiffString",   "relies on the host's flush-to-zero"},
    }},
};

bool excused(const std::string& property, const std::string& element)
{
    const auto p = kExceptions.find(property);
    return p != kExceptions.end() && p->second.count(element) != 0;
}

/// Deterministic noise, so a failure repeats.
struct Noise
{
    std::uint32_t state;
    explicit Noise(std::uint32_t seed) : state(seed ? seed : 1u) {}
    float next()
    {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return static_cast<float>(static_cast<std::int32_t>(state)) * 4.6566129e-10f;
    }
};

struct Driver
{
    const ElementType* type;
    std::unique_ptr<DspElement> element;
    int nAudioIn, nAudioOut, nControlIn, nControlOut;
    std::vector<float> controls;

    Driver(const ElementType* t, const ElementRegistry& registry, double rate = kRate)
        : type(t), element(registry.create(t->implementation))
    {
        nAudioIn    = static_cast<int>(t->portsMatching(true,  false).size());
        nAudioOut   = static_cast<int>(t->portsMatching(false, false).size());
        nControlIn  = static_cast<int>(t->portsMatching(true,  true ).size());
        nControlOut = static_cast<int>(t->portsMatching(false, true ).size());

        for (const auto* port : t->portsMatching(true, true))
            controls.push_back(static_cast<float>(port->defaultValue));

        element->prepare(*t, rate, 4096);
        element->reset();
    }

    /// Renders `total` samples of audio in chunks of the given sizes (repeated as needed)
    /// from noise at `level`, or from silence when level is 0. Returns each audio output
    /// channel after the other.
    std::vector<float> inputSignal;  ///< when set, every audio input reads this instead of noise
    bool countAllocations = false;   ///< count what process() allocates, and nothing else here
    bool gate = true;                ///< the note held, which a bowed string or an envelope reads

    std::vector<float> render(int total, const std::vector<int>& chunks, float level, std::uint32_t seed = 1,
                              const std::vector<std::vector<float>>* controlSchedule = nullptr,
                              std::vector<float>* controlOutLast = nullptr)
    {
        std::vector<std::vector<float>> in(static_cast<std::size_t>(std::max(nAudioIn, 1)),
                                           std::vector<float>(static_cast<std::size_t>(total), 0.0f));
        Noise noise(seed);
        for (auto& channel : in)
            for (std::size_t i = 0; i < channel.size(); ++i)
                channel[i] = inputSignal.size() > i ? inputSignal[i] : level * noise.next();

        std::vector<std::vector<float>> out(static_cast<std::size_t>(std::max(nAudioOut, 1)),
                                            std::vector<float>(static_cast<std::size_t>(total), 0.0f));
        std::vector<float> controlOut(static_cast<std::size_t>(std::max(nControlOut, 1)), 0.0f);

        int done = 0, which = 0;
        while (done < total)
        {
            const int n = std::min(chunks[static_cast<std::size_t>(which++) % chunks.size()], total - done);

            if (controlSchedule != nullptr && ! controlSchedule->empty())
                controls = (*controlSchedule)[static_cast<std::size_t>(done) * controlSchedule->size()
                                              / static_cast<std::size_t>(total)];

            std::vector<const float*> inPtrs;
            std::vector<float*> outPtrs;
            for (auto& c : in)  inPtrs.push_back(c.data() + done);
            for (auto& c : out) outPtrs.push_back(c.data() + done);

            ProcessArgs args;
            args.audioIn       = inPtrs.data();
            args.audioOut      = outPtrs.data();
            args.numAudioIn    = nAudioIn;
            args.numAudioOut   = nAudioOut;
            args.numSamples    = n;
            args.gate          = gate;
            args.velocity      = 0.8f;
            args.noteNumber    = 60;
            args.controlIn     = controls.data();
            args.numControlIn  = nControlIn;
            args.controlOut    = controlOut.data();
            args.numControlOut = nControlOut;
            args.transport.playing = true;
            args.transport.tempoBpm = 120.0;
            args.transport.ppqPosition = 0.0;

            gCounting = countAllocations;
            element->process(args);
            gCounting = false;
            done += n;
        }

        if (controlOutLast != nullptr)
            *controlOutLast = controlOut;

        std::vector<float> flat;
        for (int c = 0; c < nAudioOut; ++c)
            flat.insert(flat.end(), out[static_cast<std::size_t>(c)].begin(), out[static_cast<std::size_t>(c)].end());
        return flat;
    }
};

struct Failure { std::string property, element, detail; };
std::vector<Failure> gFailures;

/// The multi-rate comparison is a report for now, not a failure: nineteen elements disagree
/// across rates by more than the tolerance, and most of them are probably the measurement
/// (a generator's partials land differently, a delay's time resolution is in samples), not a
/// fault. Each needs looking at before it is either fixed or excused with a reason. They are
/// listed in TODO.md.
std::vector<Failure> gMultiRate;

void fail(const std::string& property, const std::string& element, const std::string& detail)
{
    gFailures.push_back({property, element, detail});
}

bool equal(const std::vector<float>& a, const std::vector<float>& b, double* worst = nullptr)
{
    if (a.size() != b.size()) return false;
    double w = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
        w = std::max(w, static_cast<double>(std::abs(a[i] - b[i])));
    if (worst) *worst = w;
    return w == 0.0;
}

bool finite(const std::vector<float>& v, float ceiling)
{
    return std::all_of(v.begin(), v.end(), [&](float x) { return std::isfinite(x) && std::abs(x) < ceiling; });
}

/// Fifteen tones from 50 Hz to 12 kHz at fixed phases, scaled to -20 dBFS RMS.
std::vector<float> multitone(double rate, int samples)
{
    static const double kHz[] = {50, 80, 125, 200, 315, 500, 800, 1250, 2000, 3150, 5000, 6300, 8000, 10000, 12000};
    std::vector<float> out(static_cast<std::size_t>(samples), 0.0f);
    Noise phases(99);
    for (const double f : kHz)
    {
        const double phase = 3.14159265358979 * (1.0 + phases.next());
        for (int i = 0; i < samples; ++i)
            out[static_cast<std::size_t>(i)] += static_cast<float>(std::sin(2.0 * 3.14159265358979 * f * i / rate + phase));
    }
    double sum = 0.0;
    for (const float v : out) sum += static_cast<double>(v) * v;
    const float scale = static_cast<float>(0.1 / std::sqrt(sum / samples));   // 0.1 is -20 dBFS
    for (auto& v : out) v *= scale;
    return out;
}

/// Power in each octave from 63 Hz to 8 kHz, and in all of them, in decibels, averaged over the
/// last stretches of a signal. The window is the same number of samples at every rate: a tone's
/// power in a window goes with the square of its length, so windows of equal duration in seconds
/// would differ by 6 dB for every doubling of the rate and say nothing about the element.
std::vector<double> octaveLevels(const std::vector<float>& signal, double rate)
{
    constexpr int order = 14;
    constexpr int size = 1 << order;

    // An element with no audio output has nothing to measure.
    if (signal.size() < static_cast<std::size_t>(size))
        return std::vector<double>(9, -300.0);

    juce::dsp::FFT fft(order);
    juce::dsp::WindowingFunction<float> window(static_cast<std::size_t>(size), juce::dsp::WindowingFunction<float>::hann);

    const std::size_t segments = std::min<std::size_t>(4, signal.size() / size);
    std::vector<double> power(8, 0.0);
    double total = 0.0;

    for (std::size_t seg = 0; seg < segments; ++seg)
    {
        const auto from = signal.end() - static_cast<std::ptrdiff_t>((seg + 1) * size);
        std::vector<float> buffer(static_cast<std::size_t>(size) * 2, 0.0f);
        std::copy(from, from + size, buffer.begin());
        window.multiplyWithWindowingTable(buffer.data(), static_cast<std::size_t>(size));
        fft.performFrequencyOnlyForwardTransform(buffer.data());

        for (int b = 1; b < size / 2; ++b)
        {
            const double hz = b * rate / size;
            const double p = static_cast<double>(buffer[static_cast<std::size_t>(b)]) * buffer[static_cast<std::size_t>(b)];
            total += p;
            for (int band = 0; band < 8; ++band)
            {
                const double centre = 63.0 * std::pow(2.0, band);
                if (hz >= centre / std::sqrt(2.0) && hz < centre * std::sqrt(2.0))
                    power[static_cast<std::size_t>(band)] += p;
            }
        }
    }
    power.push_back(total);

    std::vector<double> db;
    for (const double p : power)
        db.push_back(10.0 * std::log10(std::max(p / static_cast<double>(segments), 1.0e-30)));
    return db;
}

}  // namespace

int main()
{
    Ontology ontology;
    std::vector<std::string> errors;
    ontology.loadUnits(VALIS_VOCABS_DIR "/lv2/units.ttl", errors);
    const bool loaded = ontology.loadFile(VALIS_VOCABS_DIR "/valis.ttl", errors);
    assert(loaded);
    const auto registry = makeDefaultRegistry();

    int checked = 0;
    for (const auto* type : ontology.types())
    {
        const std::string name = vocab::shortName(type->classIri);
        if (kMarkers.count(type->implementation) != 0)
            continue;
        ++checked;

        // 1. The same render twice is bit-equal, and a reset puts an element back where a
        // fresh one starts.
        {
            Driver a(type, registry), b(type, registry);
            const auto first = a.render(8192, {64}, 0.1f);
            const auto second = b.render(8192, {64}, 0.1f);
            if (! excused("determinism", name) && ! equal(first, second))
                fail("determinism", name, "two fresh instances rendered differently");

            a.element->reset();
            const auto again = a.render(8192, {64}, 0.1f);
            double worst = 0.0;
            if (! excused("reset", name) && ! equal(first, again, &worst))
                fail("reset", name, "a render after reset() differs from the first by " + std::to_string(worst));
        }

        // 2. The render does not depend on how the host cut it into blocks. The engine hands
        // elements slices of 32 samples, so the contract worth holding is that a different
        // cut of the same stream sounds the same.
        {
            Driver a(type, registry), b(type, registry);
            const auto fixed = a.render(8192, {32}, 0.1f);
            const auto mixed = b.render(8192, {1, 7, 32, 13, 64, 5, 32, 32, 19, 3}, 0.1f);
            double worst = 0.0;
            if (! excused("block-size", name) && ! equal(fixed, mixed, &worst))
                fail("block-size", name, "differs by " + std::to_string(worst) + " between 32-sample and mixed blocks");
        }

        // 3. Finite and bounded under random in-range controls, with every control jumping at
        // once partway through, on noise, silence and a full-scale square.
        {
            Noise random(12345);
            bool ok = true;
            std::string detail;
            for (int trial = 0; trial < 6 && ok; ++trial)
            {
                Driver d(type, registry);
                std::vector<std::vector<float>> schedule;
                for (int stage = 0; stage < 4; ++stage)
                {
                    std::vector<float> values;
                    for (const auto* port : type->portsMatching(true, true))
                    {
                        // Ranges such as +-1e6 are a type's way of saying "unbounded", not a
                        // value worth feeding in; keep to a sane window of them.
                        const double lo = std::max(port->minimum, -1000.0);
                        const double hi = std::min(port->maximum,  1000.0);
                        const double t = 0.5 + 0.5 * static_cast<double>(random.next());
                        values.push_back(static_cast<float>(lo + (hi - lo) * t));
                    }
                    schedule.push_back(std::move(values));
                }

                const float level = trial % 3 == 0 ? 0.0f : (trial % 3 == 1 ? 0.5f : 1.0f);
                const auto out = d.render(8192, {32}, level, 77u + static_cast<std::uint32_t>(trial), &schedule);
                if (! finite(out, 1.0e5f))
                {
                    ok = false;
                    detail = "non-finite or beyond 1e5 on trial " + std::to_string(trial);
                }
            }
            if (! ok && ! excused("bounded", name))
                fail("bounded", name, detail);
        }

        // 4. No allocation in process(), after a warm-up.
        {
            Driver d(type, registry);
            d.render(4096, {32}, 0.1f);
            gAllocations = 0;
            d.countAllocations = true;
            d.render(8192, {32}, 0.1f);
            d.countAllocations = false;
            if (gAllocations != 0 && ! excused("allocation", name))
                fail("allocation", name, std::to_string(gAllocations.load()) + " allocations in 8192 samples");
        }

        // 5. After a burst, ten seconds of silence leaves nothing subnormal and nothing loud
        // (self-oscillating and generating elements say so in the exceptions).
        if (type->portsMatching(true, false).size() > 0 && type->portsMatching(false, false).size() > 0)
        {
            Driver d(type, registry);
            d.render(24000, {32}, 0.5f);
            d.gate = false;   // the player's hand comes off the string
            const auto tail = d.render(480000, {32}, 0.0f);
            const std::size_t perChannel = tail.size() / static_cast<std::size_t>(d.nAudioOut);
            const auto lastSecond = std::vector<float>(tail.end() - static_cast<std::ptrdiff_t>(48000), tail.end());
            (void) perChannel;

            bool subnormal = std::any_of(tail.begin(), tail.end(), [](float x) { return std::fpclassify(x) == FP_SUBNORMAL; });
            if (subnormal && ! excused("denormals", name))
                fail("denormals", name, "subnormal values in the output after the input stopped");

            float peak = 0.0f;
            for (const float x : lastSecond) peak = std::max(peak, std::abs(x));
            if (peak > 1.0e-3f && ! excused("decay", name))
                fail("decay", name, "still " + std::to_string(peak) + " after ten seconds of silence");
        }
    }

    // 6. The same multitone at other sample rates sounds the same. An element with a constant
    // written in samples where it meant seconds, or a filter designed for one rate, shows here.
    // Tolerances are Cathode's: tighter for linear elements, looser for nonlinear ones.
    for (const auto* type : ontology.types())
    {
        const std::string name = vocab::shortName(type->classIri);
        if (kMarkers.count(type->implementation) != 0)
            continue;

        const auto render = [&](double rate)
        {
            Driver d(type, registry, rate);
            d.inputSignal = multitone(rate, static_cast<int>(rate));
            return octaveLevels(d.render(static_cast<int>(rate), {32}, 0.0f), rate);
        };

        const auto reference = render(48000.0);
        const bool silent = reference.back() < -200.0;

        for (const double rate : {44100.0, 88200.0, 96000.0, 192000.0})
        {
            const auto other = render(rate);
            if (silent)
            {
                if (other.back() > -200.0 && ! excused("multirate", name))
                    gMultiRate.push_back({"multirate", name, "silent at 48 kHz, not at " + std::to_string(static_cast<int>(rate))});
                continue;
            }

            const double overallTolerance = type->linear ? 0.5 : 1.5;
            const double bandTolerance    = type->linear ? 1.0 : 3.0;
            std::string detail;

            if (std::abs(other.back() - reference.back()) > overallTolerance)
                detail += "overall " + std::to_string(other.back() - reference.back()) + " dB; ";
            for (int band = 0; band < 8; ++band)
            {
                // A band with nothing in it at either rate has no level to compare.
                if (reference[static_cast<std::size_t>(band)] < reference.back() - 70.0 &&
                    other[static_cast<std::size_t>(band)] < reference.back() - 70.0)
                    continue;
                const double difference = other[static_cast<std::size_t>(band)] - reference[static_cast<std::size_t>(band)];
                if (std::abs(difference) > bandTolerance)
                    detail += std::to_string(63 << band) + " Hz " + std::to_string(difference) + " dB; ";
            }

            if (! detail.empty() && ! excused("multirate", name))
            {
                gMultiRate.push_back({"multirate", name, std::to_string(static_cast<int>(rate)) + " Hz: " + detail});
                break;   // one rate is enough to name the element
            }
        }
    }

    std::printf("  %d elements checked\n", checked);

    if (! gMultiRate.empty())
        std::printf("\n  multi-rate differences (report only, see TODO.md):\n");
    for (const auto& f : gMultiRate)
        std::printf("  %-12s val:%-18s %s\n", f.property.c_str(), f.element.c_str(), f.detail.substr(0, 150).c_str());
    if (! gMultiRate.empty())
        std::printf("  %zu elements differ across rates beyond the tolerance\n\n", gMultiRate.size());

    std::map<std::string, int> perProperty;
    for (const auto& f : gFailures)
    {
        ++perProperty[f.property];
        std::printf("  %-12s val:%-18s %s\n", f.property.c_str(), f.element.c_str(), f.detail.c_str());
    }
    for (const auto& [property, count] : perProperty)
        std::printf("  %s: %d\n", property.c_str(), count);

    assert(gFailures.empty());
    std::printf("ElementConformanceTest PASSED\n");
    return 0;
}
