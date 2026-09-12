// include/valis/Console.h
//
// The console's headless command surface. ConsoleSession wraps an OpDispatcher
// the way the MCP server does: every command is a thin adapter over one Op, so
// the terminal, the views and the HTTP tools all go through the same path.
//
// Nothing here knows about a UI, a host, or a socket. The AI round-trip itself
// (HTTP) lives in the plugin target; what lives here is everything around it:
// the local command set, the extraction of Turtle code from a language-model
// reply, the validation of that code, and the system prompt that tells the
// model how to write it.
//
// Message thread only, like the ops it calls.

#pragma once

#include "valis/Ops.h"

#include <string>
#include <vector>

namespace valis {

/// What execute() returns. `clear` asks the terminal to wipe its scrollback.
struct ConsoleResult
{
    std::string text;
    bool clear = false;
};

/// A system and user prompt pair, sized against what the chosen provider will
/// accept. See docs/rate-limit-advice.md: a free tier's per-minute token budget
/// caps one request, so a circuit larger than the budget can never be sent
/// whole no matter how long the console waits.
struct AiPrompt
{
    std::string system;
    std::string user;
    long long estimatedTokens = 0;
    /// True when the element catalogue was cut back to the classes the request
    /// and the circuit actually use.
    bool catalogueCondensed = false;
    /// True when the circuit was sent as a subgraph rather than in full, which
    /// means the model's Turtle blocks are edits, not complete circuits.
    bool circuitExcerpted = false;
    std::string note;   ///< what was left out, for the console to print
};

/// One Turtle block pulled out of an AI reply, with its validation outcome.
struct PendingBlock
{
    std::string turtle;
    bool valid = false;
    std::vector<Diagnostic> diagnostics;
};

class ConsoleSession
{
public:
    explicit ConsoleSession(OpDispatcher dispatcher) : ops(std::move(dispatcher)) {}

    /// Runs one line and returns what the terminal should print. Never throws:
    /// a bad command is a message, not an exception.
    ConsoleResult execute(const std::string& line);

    /// Files an AI reply: prints the conversational text, validates each Turtle
    /// block it contains, and keeps the valid ones for `apply`. Returns the
    /// text the terminal should print. `partialContext` says the model was sent
    /// an excerpt of the circuit rather than the whole of it, in which case its
    /// blocks are edits and a block that fails validation is expected.
    std::string noteAiResponse(const std::string& reply, bool partialContext = false);

    /// Installs pending block `index` (default: the most recent valid one).
    /// A block that no longer validates is refused, not installed.
    ConsoleResult applyBlock(int index = -1);

    const std::vector<PendingBlock>& pendingBlocks() const { return pending; }

    /// Pulls fenced code out of `reply`. Accepts ```turtle blocks, and bare ```
    /// blocks that mention the val: namespace - validated afterwards anyway, so
    /// a false positive is rejected with a message rather than installed.
    /// Anything else is conversational text and is left alone.
    static std::vector<std::string> extractTurtleBlocks(const std::string& reply);

    /// The system prompt for the AI mode. The element catalogue is generated
    /// from the ontology via listElementTypes, so it cannot drift from what
    /// the compiler accepts; the rules are the same ones docs/manual/circuits.md
    /// teaches.
    static std::string buildSystemPrompt(const std::vector<ElementTypeInfo>& types);

    /// The same prompt with full port detail only for the classes in
    /// `detailedClasses` (short names, e.g. "Ladder"); the rest are listed by
    /// name alone. The catalogue is the largest fixed part of the prompt, so
    /// this is the first thing to give up when a budget is tight.
    static std::string buildSystemPrompt(const std::vector<ElementTypeInfo>& types,
                                         const std::vector<std::string>& detailedClasses);

    /// Formats the per-request user message: the raw request plus the
    /// circuit's current Turtle source as context, so the model can modify
    /// rather than start from scratch. An empty turtle passes through
    /// unchanged; no circuit is loaded yet in that case.
    static std::string buildUserPrompt(const std::string& currentTurtle,
                                       const std::string& request);

    /// System and user prompts trimmed to fit `budgetTokens`, which is what the
    /// provider's headers said its per-minute token limit is, less a margin for
    /// the reply. A budget of -1 means nothing has been measured yet and the
    /// full prompt goes out. Trimming happens in order of cost: the element
    /// catalogue first, then the circuit itself, cut to the elements the
    /// request names plus what they connect to.
    static AiPrompt buildPrompt(const std::vector<ElementTypeInfo>& types,
                                const std::string& currentTurtle,
                                const std::string& request,
                                long long budgetTokens = -1);

    /// The part of `turtle` that relates to `request`: the elements the request
    /// names, the arcs that touch them, the elements at the far end of those
    /// arcs, and the circuit declaration that names everything. Grows by whole
    /// statements until `maxChars` is reached. `summaryOut` describes what was
    /// left out, which keeps the model from assuming the circuit is what it can
    /// see. Returns the whole document unchanged when it already fits.
    static std::string excerptCircuit(const std::string& turtle,
                                      const std::string& request,
                                      std::size_t maxChars,
                                      std::string& summaryOut);

private:
    OpDispatcher ops;
    std::vector<PendingBlock> pending;
};

}  // namespace valis
