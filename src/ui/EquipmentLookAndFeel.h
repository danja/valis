// src/ui/EquipmentLookAndFeel.h
//
// A rotary-dial LookAndFeel for the Controls tab: knurled aluminium (dark
// scheme) or bakelite (light scheme) dials with silkscreen tick arcs and a
// glowing pointer line. The scheme follows the active EquipmentTheme.

#pragma once

#include "valis/UiTheme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace valis {

class EquipmentLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    EquipmentLookAndFeel() = default;

    void setScheme(const EquipmentTheme& theme);

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPosProportional,
                          float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider&) override;

private:
    juce::Colour tickInk;
    juce::Colour dialFace;
    juce::Colour dialRim;
    juce::Colour dialCap;
    juce::Colour knurlLight;
    juce::Colour knurlDark;
    juce::Colour pointer;
    juce::Colour halo;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EquipmentLookAndFeel)
};

}  // namespace valis
