// src/ui/TurtleView.cpp

#include "ui/TurtleView.h"

#include "plugin/ValisProcessor.h"

namespace valis {

namespace {
constexpr int kDebounceMs = 400;
}

TurtleView::TurtleView(ValisProcessor& p) : processor(p)
{
    p.addChangeListener(this);
    editor.setColourScheme(TurtleCodeTokeniser::darkColourScheme());
    editor.setColour(juce::CodeEditorComponent::backgroundColourId, juce::Colour(0xff1e1e22));
    editor.setColour(juce::CodeEditorComponent::lineNumberBackgroundId, juce::Colour(0xff1a1a1e));
    editor.setColour(juce::CodeEditorComponent::lineNumberTextId, juce::Colour(0xff5a6070));
    editor.setColour(juce::CodeEditorComponent::defaultTextColourId, juce::Colour(0xffabb2bf));
    editor.setColour(juce::CodeEditorComponent::highlightColourId, juce::Colour(0xff3a3f4b));
    editor.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
    editor.setScrollbarThickness(10);
    addAndMakeVisible(editor);

    refreshFromProcessor();
    document.addListener(this);
}

TurtleView::~TurtleView()
{
    processor.removeChangeListener(this);
    document.removeListener(this);
}

void TurtleView::changeListenerCallback(juce::ChangeBroadcaster*)
{
    // Only refresh when the change came from outside this view (e.g. MCP).
    // If we triggered the change ourselves, the document content already matches.
    if (processor.getTurtle() != document.getAllContent())
        refreshFromProcessor();
}

void TurtleView::refreshFromProcessor()
{
    suppressCallbacks = true;
    document.replaceAllContent(processor.getTurtle());
    document.clearUndoHistory();
    suppressCallbacks = false;

    diagnostics = processor.lastDiagnostics();
    timerCallback();
}

void TurtleView::codeDocumentTextInserted(const juce::String&, int)
{
    if (! suppressCallbacks)
        startTimer(kDebounceMs);
}

void TurtleView::codeDocumentTextDeleted(int, int)
{
    if (! suppressCallbacks)
        startTimer(kDebounceMs);
}

void TurtleView::timerCallback()
{
    stopTimer();
    recompile();
}

void TurtleView::recompile()
{
    if (! suppressCallbacks)
        processor.setTurtle(document.getAllContent(), diagnostics);

    if (diagnostics.empty())
    {
        repaint();
        return;
    }

    // The first diagnostic is the one to act on; the count says how many more
    // are waiting behind it.
    const auto& first = diagnostics.front();
    // Put the caret on the offending line so the error is where the user looks.
    if (first.line > 0)
        editor.moveCaretTo(juce::CodeDocument::Position(document,
                                                        static_cast<int>(first.line) - 1, 0),
                           false);
    repaint();
}

void TurtleView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e22));
}

void TurtleView::resized()
{
    editor.setBounds(getLocalBounds());
}

}  // namespace valis
