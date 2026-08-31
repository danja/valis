// src/ui/TurtleView.h

#pragma once

#include "ui/TurtleCodeTokeniser.h"
#include "valis/CircuitModel.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace valis {

class ValisProcessor;

/// The Turtle source view: a syntax-highlighted editor over the circuit.
///
/// Edits are debounced rather than compiled on every keystroke, and a circuit
/// that will not compile leaves the previous one playing - so the audio keeps
/// running while the text is mid-edit.
class TurtleView final : public juce::Component,
                         public juce::ChangeListener,
                         private juce::CodeDocument::Listener,
                         private juce::Timer
{
public:
    explicit TurtleView(ValisProcessor&);
    ~TurtleView() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /// Replaces the editor's text, e.g. after the graph view rewrites it.
    void refreshFromProcessor();

    void changeListenerCallback(juce::ChangeBroadcaster*) override;

private:
    void codeDocumentTextInserted(const juce::String&, int) override;
    void codeDocumentTextDeleted(int, int) override;
    void timerCallback() override;

    void recompile();

    ValisProcessor& processor;

    juce::CodeDocument document;
    TurtleCodeTokeniser tokeniser;
    juce::CodeEditorComponent editor{document, &tokeniser};

    std::vector<Diagnostic> diagnostics;
    bool suppressCallbacks = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TurtleView)
};

}  // namespace valis
