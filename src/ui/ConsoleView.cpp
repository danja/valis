// src/ui/ConsoleView.cpp

#include "ui/ConsoleView.h"

#include "ai/AiRouter.h"
#include "plugin/ValisProcessor.h"
#include "valis/AiProviders.h"
#include "valis/ProviderBudgets.h"

#include <map>
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

void ConsoleView::visibilityChanged()
{
    // Switching tabs parks keyboard focus on the tab button, and clicking
    // Send parks it on the button. A console is useless without a focused
    // entry line, so claim focus whenever the tab is shown. Hosts that gate
    // key delivery per-FX (e.g. Reaper) only forward keys to the focused
    // client component, so without this typed text never reaches the input.
    if (isShowing())
        input.grabKeyboardFocus();
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
    // Clicking Send moves focus to the button; hand it back so the next
    // line can be typed without re-clicking the entry box.
    input.grabKeyboardFocus();

    print("> " + line);

    // The AI round-trip is asynchronous and must not block the message thread,
    // so it is intercepted here rather than run through the session.
    if (line.upToFirstOccurrenceOf(" ", false, false).toLowerCase() == "ai")
    {
        const auto prompt = line.fromFirstOccurrenceOf(" ", false, false).trim();
        if (prompt.isEmpty())
        {
            print("usage: ai <prompt>  (needs an API key for a provider - see Settings)");
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

    const auto& budgets = ai::ProviderBudgets::instance();
    const std::string endpoint = processor.getAiEndpoint().toStdString();
    const auto* preset = ai::findAiProvider(endpoint);

    // Keys are read here, on the message thread, for every provider the router
    // may fall through to.
    std::map<std::string, std::string> keys;
    keys[endpoint] = processor.getAiApiKey().toStdString();
    for (const auto& provider : ai::aiProviders())
        keys[provider.endpoint] = processor.getAiApiKeyFor(
            juce::String(provider.endpoint)).toStdString();

    // A request needs a key somewhere: the chosen provider's, or another
    // provider's to fall through to. Each attempt costs free-tier budget, so
    // refuse before sending when there is none at all. A keyless local server
    // does not count towards that: the router may still try it, but the hint
    // about where to get a key is worth printing when nothing else is set up.
    bool anyKey = ! keys[endpoint].empty() || (preset != nullptr && ! preset->needsKey);
    for (const auto& provider : ai::aiProviders())
        anyKey = anyKey || (provider.needsKey && ! keys[provider.endpoint].empty());
    if (! anyKey)
    {
        aiBusy.store(false);
        juce::String hint = "Settings > Set API Key...";
        if (preset != nullptr && ! preset->keyHint.empty())
            hint = juce::String(preset->keyHint) + " (" + hint + ")";
        print("[no API key set - " + hint + "]");
        return;
    }

    // Snapshot everything the background thread needs while still on the
    // message thread; it must not touch the processor, the session or JUCE
    // components afterwards.
    const std::string model  = processor.getAiModel().toStdString();
    const auto turtleResult  = processor.ops().getTurtle();

    // Size the prompt against whatever this provider's headers said its budget
    // is, leaving a quarter of it for the reply. Nothing measured yet means the
    // whole prompt goes out and the first response says what the limit is.
    const auto measured = budgets.budget(endpoint);
    const long long allowance = measured.limitKnown()
        ? static_cast<long long>(static_cast<double>(measured.limitTokens) * 0.75)
        : -1;
    const auto built = ConsoleSession::buildPrompt(
        processor.ops().listElementTypes(),
        turtleResult.ok ? turtleResult.value : std::string{},
        prompt, allowance);

    if (! built.note.empty())
        print("[" + juce::String(built.note) + "]");

    ai::RouteRequest request{endpoint, model, built.system, built.user};

    print("[asking " + juce::String(model) + " (about " +
          juce::String(built.estimatedTokens) + " tokens) ...]");

    juce::Component::SafePointer<ConsoleView> safe(this);
    const bool partial = built.circuitExcerpted;
    std::thread([safe, request, keys, partial]
    {
        const auto lookup = [&keys](const ai::AiProvider& provider) -> std::string
        {
            const auto found = keys.find(provider.endpoint);
            return found == keys.end() ? std::string{} : found->second;
        };
        const auto result = ai::route(request, lookup, ai::ProviderBudgets::instance());

        juce::MessageManager::callAsync([safe, result, partial]
        {
            if (safe != nullptr)
                safe->finishAiRequest(result, partial);
        });
    }).detach();
}

void ConsoleView::finishAiRequest(const ai::RouteResult& result, bool partialContext)
{
    aiBusy.store(false);

    // The trace says which providers were skipped and why. It is empty on the
    // ordinary path, where the chosen provider answered first time.
    for (const auto& line : result.trace)
        print(juce::String(line));

    if (result.result.ok)
    {
        if (! result.trace.empty())
            print("[answered by " + juce::String(result.providerName) + "]");
        print(session.noteAiResponse(result.result.reply, partialContext));
    }
    else
    {
        print("[AI request failed: " + juce::String(result.result.error) + "]");
    }
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
