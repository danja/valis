// src/ui/ConsoleView.h
//
// The Console tab: a terminal attached to a ConsoleSession REPL, plus the AI
// mode that asks a language model to design circuits.
//
// Threading: local commands run on the message thread through the session's
// ops. An `ai` request runs ai::chat on a short-lived background thread with
// snapshots taken on the message thread - the audio thread is never involved,
// and at most one request is ever in flight.

#pragma once

#include "ai/MistralClient.h"
#include "valis/Console.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <atomic>
#include <string>
#include <vector>

namespace valis {

class ValisProcessor;

class ConsoleView final : public juce::Component,
                          private juce::KeyListener
{
public:
    explicit ConsoleView(ValisProcessor&);
    ~ConsoleView() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    void sendLine(const juce::String& line);
    void startAiRequest(const std::string& prompt);
    void finishAiRequest(const ai::ChatResult& result);
    void print(const juce::String& text);

    bool keyPressed(const juce::KeyPress&, juce::Component*) override;

    ValisProcessor& processor;
    ConsoleSession session;

    juce::TextEditor output;   ///< read-only scrollback
    juce::TextEditor input;    ///< one line in, Return sends
    juce::TextButton sendButton{"Send"};

    std::vector<std::string> history;
    int historyIndex = -1;     ///< -1 means not browsing history

    std::atomic<bool> aiBusy{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConsoleView)
};

}  // namespace valis
