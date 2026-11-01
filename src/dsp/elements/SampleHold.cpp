// src/dsp/elements/SampleHold.cpp
//
// val:SampleHold copies its input when its trigger rises and holds the copy until the next rise.
// https://en.wikipedia.org/wiki/Sample_and_hold
//
// Its use here is to keep a note's velocity for as long as the sound lasts. A val:NoteGate reports
// the velocity only while the note is held and 0 after the note-off, so a drum that rings on after
// a short note cannot take its level from it directly; the held copy lasts until the next hit.
//
// The inputs are read once per control slice, so a trigger that rises and falls inside one slice
// is not seen, and the input is the value it has in the slice where the trigger rises.

#include "Common.h"

namespace valis::elements {

class SampleHold final : public DspElement
{
public:
    void prepare(const ElementType& type, double, int) override
    {
        inIndex      = controlIndex(type, "in");
        triggerIndex = controlIndex(type, "trigger");
        reset();
    }

    void reset() override
    {
        held = 0.0f;
        wasHigh = false;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        const bool high = controlAt(args, triggerIndex, 0.0f) > 0.5f;
        if (high && ! wasHigh)
            held = controlAt(args, inIndex, 0.0f);
        wasHigh = high;

        if (args.numControlOut > 0)
            args.controlOut[0] = held;
    }

private:
    float held = 0.0f;
    bool  wasHigh = false;
    int   inIndex = -1, triggerIndex = -1;
};

}  // namespace valis::elements

namespace valis {
namespace {
template <typename T> std::unique_ptr<DspElement> make() { return std::make_unique<T>(); }
}  // namespace

void registerSampleHold(ElementRegistry& registry)
{
    registry.add("SampleHold", &make<elements::SampleHold>);
}

}  // namespace valis
