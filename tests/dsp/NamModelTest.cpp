// tests/dsp/NamModelTest.cpp
//
// val:NamModel: loading failures are located messages, bypass is exact, and
// once a model is loaded process() allocates nothing for any architecture.

// Asserts are the test; JUCE defines NDEBUG in every non-Debug configuration.
#undef NDEBUG

#include "valis/ElementTestFixture.h"

#include <juce_cryptography/juce_cryptography.h>

#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>

using namespace valis;

namespace {
std::atomic<bool> counting{false};
std::atomic<long> allocations{0};
}  // namespace

// Counts every allocation made while `counting` is set, from any thread.
void* operator new(std::size_t size)
{
    if (counting.load(std::memory_order_relaxed))
        allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size))
        return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {

constexpr double kRate = 48000.0;

/// Runs `blocks` slices of `slice` frames of a sine through the element and
/// returns the number of allocations made while doing so.
long runSlices(DspElement& element, const ElementType& type, int blocks, int slice,
               std::vector<float>& captured)
{
    std::vector<float> in(static_cast<std::size_t>(slice)), out(static_cast<std::size_t>(slice));
    std::vector<float> controls;
    for (const auto* port : type.portsMatching(true, true))
        controls.push_back(static_cast<float>(port->defaultValue));

    const float* inPtr = in.data();
    float* outPtr = out.data();
    captured.clear();
    captured.reserve(static_cast<std::size_t>(blocks * slice));

    ProcessArgs args;
    args.audioIn = &inPtr;
    args.audioOut = &outPtr;
    args.numAudioIn = 1;
    args.numAudioOut = 1;
    args.numSamples = slice;
    args.controlIn = controls.data();
    args.numControlIn = static_cast<int>(controls.size());

    allocations = 0;
    long total = 0;
    long phase = 0;
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < slice; ++i, ++phase)
            in[static_cast<std::size_t>(i)] =
                0.3f * std::sin(6.2831853f * 220.0f * static_cast<float>(phase) / static_cast<float>(kRate));

        counting = true;
        element.process(args);
        counting = false;
        total += allocations.exchange(0);

        captured.insert(captured.end(), out.begin(), out.end());
    }
    return total;
}

/// Nothing about a model file fails the circuit: a file that is missing, not a
/// model, or trained at another rate leaves the element passing its input
/// through, with the reason in status(), so the Controls view can offer a chooser.
void testBadModelsDoNotRefuseTheCircuit()
{
    std::string error;
    const std::vector<float> in(256, 0.25f);

    for (const char* path : {"/no/such/model.nam", VALIS_ROOT_DIR "/vocabs/valis.ttl"})
    {
        ElementTestFixture rig("NamModel", kRate);
        assert(rig.element->status().empty());

        assert(rig.element->setOption("unrelated", "x", error));   // an unknown key is not a problem
        assert(rig.element->status().empty());

        assert(rig.element->setOption("model", path, error));
        assert(! rig.element->status().empty());
        std::printf("  %s: %s\n", path, rig.element->status().c_str());
        assert(rig.run(in) == in);
    }
}

void testPassesThroughWithoutAModel()
{
    ElementTestFixture rig("NamModel", kRate);
    std::vector<float> in(256);
    for (std::size_t i = 0; i < in.size(); ++i)
        in[i] = 0.01f * static_cast<float>(i % 17) - 0.08f;

    assert(rig.run(in) == in);
}

#ifdef VALIS_WITH_NAM
void testModelsRunWithoutAllocating()
{
    const char* models[] = {"wavenet.nam", "lstm.nam", "A2.nam", "wavenet_a1_standard.nam"};

    for (const char* name : models)
    {
        ElementTestFixture rig("NamModel", kRate);
        std::string error;
        const std::string path = std::string(VALIS_NAM_MODELS_DIR) + "/" + name;

        const bool loaded = rig.element->setOption("model", path, error);
        if (! loaded)
            std::printf("  %s: %s\n", name, error.c_str());
        assert(loaded);

        const auto* type = rig.type;
        std::vector<float> out;

        // 32 frames is the engine's control slice; 4096 is the largest block
        // the fixture prepared the element for.
        for (const int slice : {32, 480, 4096})
        {
            const long allocated = runSlices(*rig.element, *type, 40, slice, out);
            std::printf("  %-26s slice %4d: %ld allocations\n", name, slice, allocated);
            assert(allocated == 0);

            for (float s : out)
                assert(std::isfinite(s));
            assert(ElementTestFixture::measureRms(out) > 1.0e-4f);
        }
    }
}

/// A model trained at another rate is a problem, not a failure, and a good file
/// afterwards clears it.
void testWrongRateIsAProblem()
{
    ElementTestFixture rig("NamModel", 44100.0);
    std::string error;
    assert(rig.element->setOption("model", VALIS_NAM_MODELS_DIR "/wavenet.nam", error));
    assert(rig.element->status().find("44100") != std::string::npos);

    ElementTestFixture right("NamModel", kRate);
    assert(right.element->setOption("model", "/no/such/model.nam", error));
    assert(! right.element->status().empty());
    assert(right.element->setOption("model", VALIS_NAM_MODELS_DIR "/wavenet.nam", error));
    assert(right.element->status().empty());
}

/// A declared hash that matches lets the model run; one that does not leaves the
/// element passing the input through with the reason given, and it does not matter
/// whether the hash or the file is named first.
void testDeclaredHashIsChecked()
{
    const std::string path = VALIS_NAM_MODELS_DIR "/wavenet.nam";
    juce::FileInputStream stream{juce::File(path)};
    const std::string actual = juce::SHA256(stream).toHexString().toStdString();
    const std::string wrong(64, 'a');

    std::string error;
    for (const bool hashFirst : { true, false })
    {
        ElementTestFixture good("NamModel", kRate);
        if (hashFirst) assert(good.element->setOption("sha256", actual, error));
        assert(good.element->setOption("model", path, error));
        if (! hashFirst) assert(good.element->setOption("sha256", actual, error));
        assert(good.element->status().empty());

        ElementTestFixture bad("NamModel", kRate);
        if (hashFirst) assert(bad.element->setOption("sha256", wrong, error));
        assert(bad.element->setOption("model", path, error));
        if (! hashFirst) assert(bad.element->setOption("sha256", wrong, error));
        assert(bad.element->status().find("not the file declared") != std::string::npos);

        // And it passes the input through rather than running the unverified model.
        std::vector<float> in(256, 0.25f);
        assert(bad.run(in) == in);
    }

    ElementTestFixture malformed("NamModel", kRate);
    assert(! malformed.element->setOption("sha256", "not a hash", error));
    assert(error.find("64 hexadecimal") != std::string::npos);
}

void testBypassIsExact()
{
    ElementTestFixture rig("NamModel", kRate);
    std::string error;
    const bool loaded = rig.element->setOption("model", VALIS_NAM_MODELS_DIR "/wavenet.nam", error);
    assert(loaded);

    rig.set("bypass", 1.0f);
    std::vector<float> in(512);
    for (std::size_t i = 0; i < in.size(); ++i)
        in[i] = std::sin(0.05f * static_cast<float>(i));
    assert(rig.run(in) == in);
}
#endif

}  // namespace

int main()
{
    testBadModelsDoNotRefuseTheCircuit();
    testPassesThroughWithoutAModel();
#ifdef VALIS_WITH_NAM
    testModelsRunWithoutAllocating();
    testWrongRateIsAProblem();
    testDeclaredHashIsChecked();
    testBypassIsExact();
#endif
    std::puts("NamModelTest passed");
    return 0;
}
