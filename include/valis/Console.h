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
    /// text the terminal should print.
    std::string noteAiResponse(const std::string& reply);

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

    /// Formats the per-request user message: the raw request plus the
    /// circuit's current Turtle source as context, so the model can modify
    /// rather than start from scratch. An empty turtle passes through
    /// unchanged; no circuit is loaded yet in that case.
    static std::string buildUserPrompt(const std::string& currentTurtle,
                                       const std::string& request);

private:
    OpDispatcher ops;
    std::vector<PendingBlock> pending;
};

}  // namespace valis
