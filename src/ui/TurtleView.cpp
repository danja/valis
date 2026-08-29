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

    status.setJustificationType(juce::Justification::centredLeft);
    status.setFont(juce::FontOptions(13.0f));
    addAndMakeVisible(status);

    revertButton.onClick = [this] { refreshFromProcessor(); };
    addAndMakeVisible(revertButton);

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
        status.setColour(juce::Label::textColourId, juce::Colour(0xff98c379));
        status.setText("ok", juce::dontSendNotification);
        repaint();
        return;
    }

    // The first diagnostic is the one to act on; the count says how many more
    // are waiting behind it.
    const auto& first = diagnostics.front();
    juce::String text = first.toString();
    if (diagnostics.size() > 1)
        text += "   (+" + juce::String(diagnostics.size() - 1) + " more)";

    status.setColour(juce::Label::textColourId, juce::Colour(0xffe06c75));
    status.setText(text, juce::dontSendNotification);

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

    // A coloured rule under the editor, so the status reads as a gutter rather
    // than as another line of text.
    auto bar = getLocalBounds().removeFromBottom(30);
    g.setColour(diagnostics.empty() ? juce::Colour(0xff2a3a2e) : juce::Colour(0xff3a2626));
    g.fillRect(bar);
}

void TurtleView::resized()
{
    auto bounds = getLocalBounds();
    auto bar = bounds.removeFromBottom(30).reduced(8, 4);
    revertButton.setBounds(bar.removeFromRight(70));
    bar.removeFromRight(8);
    status.setBounds(bar);
    editor.setBounds(bounds);
}

}  // namespace valis
