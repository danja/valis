// src/ui/SamplerBox.h
//
// The panel of one val:Sampler: a strip per instrument with level, pan, key, name and sound, and buttons to add
// sounds, copy the table or go back to the circuit's own. A change rewrites the table text and installs it as
// the session's choice for the node (ValisProcessor::setSample), exactly as a file chosen for a val:SampleLoad
// is; the circuit's document is not edited. Owned by ControlsView. Message thread only.

#pragma once

#include "ui/PadStrip.h"
#include "valis/UiTheme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace valis {

class ValisProcessor;

class SamplerBox final : public juce::Component
{
public:
    SamplerBox(ValisProcessor& processor, std::string nodeId, juce::String title);
    ~SamplerBox() override;

    void setTheme(const EquipmentTheme& t);

    /// Re-reads the table, and rebuilds the strips if the number of instruments changed.
    void refresh();

    /// The height this box needs for the instruments it holds, at the width it has.
    int preferredHeight() const;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void commit(const std::function<bool(SampleTable&)>& change);
    void request(std::function<bool(SampleTable&)> change);   ///< commit, once the caller has returned
    void addSounds();
    void chooseFor(int pad);
    std::string pathFor(const juce::File&) const;
    void rebuildStrips();

    ValisProcessor& processor;
    std::string nodeId;
    juce::String title;
    EquipmentTheme theme = themeByName("Dark");

    std::string shown;                              ///< the table text the strips show
    std::vector<std::unique_ptr<PadStrip>> strips;
    juce::TextButton addButton{"Add sounds..."}, copyButton{"Copy table"}, revertButton{"Use circuit's kit"};
    juce::String message;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerBox)
};

}  // namespace valis
