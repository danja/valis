// tests/dsp/NamModelTest.cpp
//
// val:NamModel: loading failures are located messages, bypass is exact, and
// once a model is loaded process() allocates nothing for any architecture.

#include "valis/ElementTestFixture.h"

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

void testLoadFailuresAreLocated()
{
    ElementTestFixture rig("NamModel", kRate);
    std::string error;

    assert(rig.element->setOption("unrelated", "x", error));      // an unknown key is not a failure

    assert(! rig.element->setOption("model", "/no/such/model.nam", error));
    assert(error.find("/no/such/model.nam") != std::string::npos
           || error.find("NAM support") != std::string::npos);

    error.clear();
    assert(! rig.element->setOption("model", VALIS_ROOT_DIR "/vocabs/valis.ttl", error));
    assert(! error.empty());
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

        const auto* type = getOntology().find(vocab::valTerm("NamModel"));
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
    testLoadFailuresAreLocated();
    testPassesThroughWithoutAModel();
#ifdef VALIS_WITH_NAM
    testModelsRunWithoutAllocating();
    testBypassIsExact();
#endif
    std::puts("NamModelTest passed");
    return 0;
}
