// src/ui/ScopeBox.h
//
// Graphic oscilloscope for one val:Oscilloscope node: a view box the size of
// two knobs showing the recent waveform plus the peak/RMS/frequency readout.
// Owned by ControlsView, which rebuilds it per circuit and refreshes it on
// the view timer (message thread only).

#pragma once

#include "valis/UiTheme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <string>
#include <vector>

namespace valis {

class ValisProcessor;

class ScopeBox final : public juce::Component
{
public:
    /// Two knob cells wide, one tall: 2 x 104 by 152.
    static constexpr int kWidth  = 208;
    static constexpr int kHeight = 152;

    ScopeBox(ValisProcessor& processor, std::string nodeId, juce::String title);

    void setTheme(const EquipmentTheme& theme) { this->theme = theme; }

    /// Message thread. Pulls the latest tap samples and control values, then
    /// repaints. Cheap enough to run at the view's refresh rate.
    void refresh();

    void paint(juce::Graphics& g) override;

private:
    ValisProcessor& processor;
    std::string nodeId;
    juce::String title;
    EquipmentTheme theme = themeByName("Dark");

    static constexpr int kSamples = 1024;
    std::vector<float> samples;
    juce::String readout = "Peak: --   RMS: --   Freq: --";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScopeBox)
};

}  // namespace valis
