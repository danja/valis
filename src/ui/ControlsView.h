// src/ui/ControlsView.h

#pragma once

#include "ui/EquipmentLookAndFeel.h"
#include "valis/UiTheme.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace valis {

class ValisProcessor;
class ScopeBox;
class SpectrumBox;

/// Knobs for the circuit's bound parameter slots, a graphic oscilloscope per
/// val:Oscilloscope element and a spectrum analyzer per val:FreqAnalyzer
/// element. Loading a new circuit rebuilds the panel.
class ControlsView final : public juce::Component,
                           public juce::ChangeListener,
                           private juce::Timer
{
public:
    explicit ControlsView(ValisProcessor&);
    ~ControlsView() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void parentSizeChanged() override;

    void rebuild();
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

private:
    void timerCallback() override;
    void applyTheme();

    struct Knob
    {
        // Exactly one of slider/comboBox is non-null, depending on the port type.
        std::unique_ptr<juce::Slider>   slider;
        std::unique_ptr<juce::ComboBox> comboBox;
        std::unique_ptr<juce::Label>    name;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   attachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
        juce::String target, unit, sectionName;
        double minimum = 0.0, maximum = 1.0;

        bool isEnum() const { return comboBox != nullptr; }

        /// The dial's normalised position rendered in the property's units.
        juce::String readout() const;
    };

    /// Label drawn above a section's column span.
    struct SectionHeader { int x, y, w; juce::String name; };

    /// Vertical divider drawn between two adjacent sections that share a row.
    struct VertDiv { int x, yTop, yBot; };

    ValisProcessor& processor;
    EquipmentLookAndFeel equipmentLnf;  ///< knob dials; set on this view
    EquipmentTheme theme = themeByName("Dark");
    std::vector<Knob> knobs;
    std::vector<std::unique_ptr<ScopeBox>> scopes;
    std::vector<std::unique_ptr<SpectrumBox>> spectrums;
    std::vector<SectionHeader> sectionHeaders;
    std::vector<VertDiv> vertDivs;
    juce::Label emptyMessage;
    std::vector<std::string> lastElementIds;
    std::vector<std::string> lastParamKeys;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlsView)
};

}  // namespace valis
