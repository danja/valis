// src/ui/ValisEditor.h

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace valis {

class ValisProcessor;

/// The three views the brief asks for, in one tabbed container: a Turtle source
/// editor, a node-and-arc graph, and a knob panel.
class ValisEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ValisEditor(ValisProcessor&);
    ~ValisEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    ValisProcessor& processor;
    juce::TabbedComponent tabs{juce::TabbedButtonBar::TabsAtTop};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ValisEditor)
};

}  // namespace valis
