// src/ui/PadStrip.h
//
// One instrument of a val:Sampler in its panel: audition, the keys it answers, its name, the sound it plays,
// a level, a pan and a way to remove it. It holds no table; it reports what the user did and SamplerBox
// writes that into the table. Message thread only.

#pragma once

#include "valis/SampleTable.h"
#include "valis/UiTheme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace valis {

class PadStrip final : public juce::Component
{
public:
    static constexpr int kHeight = 38;

    PadStrip();

    /// Shows a pad. Does not call back: only the user's own change does.
    void show(const SamplePad& pad);
    void setTheme(const EquipmentTheme& t);

    std::function<void(bool down)> onAudition;
    std::function<void(float levelDb)> onLevel;
    std::function<void(float pan)> onPan;
    std::function<void(const juce::String&)> onName;
    std::function<void(const juce::String&)> onKeys;
    std::function<void()> onLoad;
    std::function<void()> onRemove;

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    struct HoldButton final : juce::TextButton
    {
        std::function<void(bool)> held;
        void mouseDown(const juce::MouseEvent& e) override { juce::TextButton::mouseDown(e); if (held) held(true); }
        void mouseUp(const juce::MouseEvent& e) override { juce::TextButton::mouseUp(e); if (held) held(false); }
    };

    void styleSlider(juce::Slider&);

    HoldButton audition;
    juce::TextEditor keys, name;
    juce::TextButton load{"Load..."}, remove{"x"};
    juce::Label fileLabel;
    juce::Slider level, pan;
    bool dragging = false;
    bool showing = false;   ///< true while show() sets the controls, so that does not read as a user change
    EquipmentTheme theme = themeByName("Dark");

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadStrip)
};

}  // namespace valis
