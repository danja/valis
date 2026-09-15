// src/ui/ToggleSwitch.h
//
// A two-position rocker for a binary control port. A knob is the wrong shape
// for a choice between two things: it invites a sweep, shows no detent, and its
// readout is a number where the port has a name for each position.
//
// Owned by ControlsView. Message thread only.

#pragma once

#include "valis/UiTheme.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace valis {

class ToggleSwitch final : public juce::Component
{
public:
    /// `offLabel` and `onLabel` are the port's own names for its two positions,
    /// from its scale points where it declares them.
    ToggleSwitch(juce::RangedAudioParameter& parameter,
                 juce::String offLabel, juce::String onLabel);

    void setTheme(const EquipmentTheme& t) { theme = t; repaint(); }

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    /// The parameter is normalised 0 to 1 and the switch uses both ends of it,
    /// so on is simply the upper half.
    bool isOn() const { return parameter.getValue() > 0.5f; }

    juce::RangedAudioParameter& parameter;
    juce::ParameterAttachment attachment;
    juce::String offLabel, onLabel;
    EquipmentTheme theme = themeByName("Dark");

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToggleSwitch)
};

}  // namespace valis
