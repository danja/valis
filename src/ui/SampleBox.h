// src/ui/SampleBox.h
//
// The file slot for one val:SampleLoad node: the name of the sound file it is
// playing, and a button to choose another. Owned by ControlsView, which
// rebuilds it per circuit. Message thread only.

#pragma once

#include "valis/UiTheme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <string>

namespace valis {

class ValisProcessor;

class SampleBox final : public juce::Component
{
public:
    /// Two knob cells wide, one tall, so it sits in the same grid as a scope.
    static constexpr int kWidth  = 208;
    static constexpr int kHeight = 152;

    SampleBox(ValisProcessor& processor, std::string nodeId, juce::String title);
    ~SampleBox() override;

    void setTheme(const EquipmentTheme& t);

    /// Re-reads what the node is playing, in case it changed elsewhere.
    void refresh();

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void chooseFile();

    ValisProcessor& processor;
    std::string nodeId;
    juce::String title;
    juce::String fileName = "(no file)";
    juce::String message;
    EquipmentTheme theme = themeByName("Dark");

    juce::TextButton loadButton{"Load..."};
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleBox)
};

}  // namespace valis
