// src/ui/SelectorStrip.h
//
// A row of labelled positions for a control port that offers a fixed set of
// choices. Every option is on the panel at once, named, and one click away,
// which a dropdown cannot claim: it hides the alternatives behind a menu and
// shows nothing about how many there are.
//
// Owned by ControlsView. Message thread only.

#pragma once

#include "valis/UiTheme.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <utility>
#include <vector>

namespace valis {

class SelectorStrip final : public juce::Component
{
public:
    /// `choices` are the port's scale points: the port value each position
    /// stands for, and its name. `minimum` and `maximum` are the port's range,
    /// which is what the parameter's normalised position is measured against.
    SelectorStrip(juce::RangedAudioParameter& parameter,
                  std::vector<std::pair<double, juce::String>> choices,
                  double minimum, double maximum);

    void setTheme(const EquipmentTheme& t) { theme = t; repaint(); }

    std::size_t getNumChoices() const { return choices.size(); }

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    /// The choice nearest the parameter's current value. Nearest rather than
    /// equal: a host automating the slot can land between two positions.
    int selectedIndex() const;
    juce::Rectangle<float> cellBounds(int index) const;

    juce::RangedAudioParameter& parameter;
    juce::ParameterAttachment attachment;
    std::vector<std::pair<double, juce::String>> choices;
    double minimum = 0.0, maximum = 1.0;
    EquipmentTheme theme = themeByName("Dark");

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SelectorStrip)
};

}  // namespace valis
