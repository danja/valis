// src/ui/ConsoleView.cpp

#include "ui/ConsoleView.h"

#include "ai/MistralClient.h"
#include "plugin/ValisProcessor.h"

#include <thread>

namespace valis {

ConsoleView::ConsoleView(ValisProcessor& p)
    : processor(p), session(p.ops())
{
    output.setMultiLine(true);
    output.setReadOnly(true);
    output.setScrollbarsShown(true);
    output.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    output.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff141416));
    output.setColour(juce::TextEditor::textColourId, juce::Colour(0xffabb2bf));
    output.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff2a2d34));
    addAndMakeVisible(output);

    input.setMultiLine(false);
    input.setReturnKeyStartsNewLine(false);
    input.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    input.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff1e1e22));
    input.setColour(juce::TextEditor::textColourId, juce::Colour(0xffd7dae0));
    input.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff2a2d34));
    input.addKeyListener(this);
    addAndMakeVisible(input);

    sendButton.onClick = [this] { sendLine(input.getText()); };
    addAndMakeVisible(sendButton);

    print("Valis console - type: help");
}

ConsoleView::~ConsoleView() = default;

void ConsoleView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e22));
}

void ConsoleView::resized()
{
    auto bounds = getLocalBounds().reduced(8);
    auto entry = bounds.removeFromBottom(28);
    sendButton.setBounds(entry.removeFromRight(64));
    entry.removeFromRight(8);
    input.setBounds(entry);
    bounds.removeFromBottom(8);
    output.setBounds(bounds);
}

void ConsoleView::print(const juce::String& text)
{
    if (text.isEmpty())
        return;
    output.moveCaretToEnd();
    output.insertTextAtCaret(text + "\n");
    output.moveCaretToEnd();
}

void ConsoleView::sendLine(const juce::String& rawLine)
{
    const auto line = rawLine.trim();
    if (line.isEmpty())
        return;

    history.push_back(line.toStdString());
    historyIndex = -1;
    input.clear();

    print("> " + line);

    // The AI round-trip is asynchronous and must not block the message thread,
    // so it is intercepted here rather than run through the session.
    if (line.upToFirstOccurrenceOf(" ", false, false).toLowerCase() == "ai")
    {
        const auto prompt = line.fromFirstOccurrenceOf(" ", false, false).trim();
        if (prompt.isEmpty())
        {
            print("usage: ai <prompt>  (needs a Mistral API key - see Settings)");
            return;
        }
        startAiRequest(prompt.toStdString());
        return;
    }

    const auto result = session.execute(line.toStdString());
    if (result.clear)
        output.clear();
    print(result.text);
}

void ConsoleView::startAiRequest(const std::string& prompt)
{
    if (aiBusy.exchange(true))
    {
        print("[an AI request is already running - wait for it to finish]");
        return;
    }

    if (processor.getAiApiKey().isEmpty() &&
        processor.getAiEndpoint().contains("api.mistral.ai"))
    {
        aiBusy.store(false);
        print("[no API key set - Settings > Set Mistral API Key...]");
        return;
    }

    // Snapshot everything the background thread needs while still on the
    // message thread; it must not touch the processor, the session or JUCE
    // components afterwards.
    const std::string endpoint = processor.getAiEndpoint().toStdString();
    const std::string apiKey   = processor.getAiApiKey().toStdString();
    const std::string model    = processor.getAiModel().toStdString();
    const std::string system   = ConsoleSession::buildSystemPrompt(processor.ops().listElementTypes());

    print("[asking " + juce::String(model) + " ...]");

    juce::Component::SafePointer<ConsoleView> safe(this);
    std::thread([safe, endpoint, apiKey, model, system, prompt]
    {
        const auto result = ai::chat(endpoint, apiKey, model, system, prompt);

        juce::MessageManager::callAsync([safe, result]
        {
            if (safe != nullptr)
                safe->finishAiRequest(result);
        });
    }).detach();
}

void ConsoleView::finishAiRequest(const ai::ChatResult& result)
{
    aiBusy.store(false);
    if (result.ok)
        print(session.noteAiResponse(result.reply));
    else
        print("[AI request failed: " + juce::String(result.error) + "]");
}

bool ConsoleView::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    if (key == juce::KeyPress::returnKey)
    {
        sendLine(input.getText());
        return true;
    }
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
    {
        if (history.empty())
            return false;
        if (historyIndex < 0)
            historyIndex = static_cast<int>(history.size());
        historyIndex += (key == juce::KeyPress::upKey) ? -1 : 1;
        historyIndex = juce::jlimit(0, static_cast<int>(history.size()), historyIndex);
        input.setText(historyIndex < static_cast<int>(history.size())
                          ? juce::String(history[static_cast<std::size_t>(historyIndex)])
                          : juce::String{});
        input.moveCaretToEnd();
        return true;
    }
    return false;
}

}  // namespace valis
