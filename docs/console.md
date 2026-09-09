# Valis Console

The Console is a fourth tab in the Valis UI: a terminal attached to a REPL.
Local commands inspect and drive the loaded circuit; an AI mode asks a
language model to design circuits, validates whatever comes back, and installs
it only on request.

## Local commands

Every command is a thin adapter over one `Op` from `include/valis/Ops.h`, so
the terminal passes through exactly what the text editor and the MCP server
pass through. Type `help` in the tab for the list:

| Command | Op | Effect |
|---|---|---|
| `stats` | `getDiagnostics` | element, arc and param counts plus latency |
| `turtle` | `getTurtle` | the circuit's current Turtle source |
| `types [filter]` | `listElementTypes` | element classes with ports, ranges and units |
| `params` | `listParams` | bindings with live values |
| `get <slot>` | `getParam` | one parameter in its own units |
| `set <slot> <val>` | `setParam` | one parameter in its own units |
| `validate` | `validate` | check the current Turtle without reinstalling it |
| `blocks` | - | Turtle blocks the AI proposed, with validation |
| `apply [n]` | `setTurtle` | install block `n` (default: most recent valid one) |
| `clear` | - | wipe the scrollback |

Input history survives across lines: Up and Down browse it, Return sends.

## AI mode

`ai <prompt>` sends the prompt to a chat-completions endpoint together with a
system prompt, and prints the reply inline. The default endpoint is the
Mistral API (`https://api.mistral.ai/v1/chat/completions`, model
`mistral-small-latest`), which has a free Experiment tier; any
OpenAI-compatible URL works, including a local server.

Turtle code and conversational text are told apart by fencing. Only
```` ```turtle ```` blocks (and bare ```` ``` ```` blocks that mention `val:`)
are treated as circuits; everything else is printed as prose. Each extracted
block is validated immediately and the verdict is printed with it:

```
[block 0: valid (4 nodes) - install with: apply 0]
[block 1: invalid:
Tanh has no port 'nosuch']
```

`apply` revalidates on the way in, so a stale block is refused rather than
half-installed. A circuit that will not compile leaves the previous one
playing, exactly as a bad edit in the Code tab does.

### System prompt

`ConsoleSession::buildSystemPrompt` assembles it from two sources: the rules in
`docs/manual/circuits.md` (one output, mixer-only fan-in, `val:UnitDelay` in
cycles, control arcs replace, the drum-velocity routing rule) and an element
catalogue generated from `listElementTypes`, so the model is never told about
a class or port the compiler would reject. It ends with a minimal working
example and an output contract: short explanation, then each complete circuit
in its own ```` ```turtle ```` block, no invented classes or ports.

### Setup

Settings menu: `AI Provider` picks a preset endpoint and model (Mistral,
Groq, Gemini, OpenRouter, or a local Ollama server), then `Set API Key...`,
`Set AI Model...` and `Set AI Endpoint...` refine it. Values persist in the
local `ApplicationProperties` between sessions. The key also honours the `VALIS_MISTRAL_API_KEY` environment
variable when nothing is stored. The key is never written to DAW state, so a
saved project cannot leak it.

## Resources and isolation

The AI mode is experimental and stays out of the audio path by construction:

- No request starts unless the user types `ai ...`. There is no polling, no
  background refresh, and at most one request in flight; a second `ai` while
  one runs is refused with a message.
- The HTTP round-trip runs on a detached worker thread against snapshots taken
  on the message thread. It never touches the processor, the session, or the
  audio thread. Completion returns via `MessageManager::callAsync`.
- The transport is one short-lived `curl` child process per request, with the
  body passed via a temporary file. This keeps HTTPS support without adding a
  TLS library to the build: the plugin still compiles with `JUCE_USE_CURL=0`
  and no new link dependency. If `curl` is missing, the console says so.

## Files

| File | Role |
|---|---|
| `include/valis/Console.h`, `src/console/Console.cpp` | `ConsoleSession`: commands, extraction, validation, system prompt. In `valis_core`, no UI dependency, covered by `tests/console/ConsoleTest.cpp` |
| `src/ai/MistralClient.h/.cpp` | request builder, reply parser, curl transport with injectable stub. Plugin target only, covered by `tests/ai/MistralClientTest.cpp` |
| `src/ui/ConsoleView.h/.cpp` | the tab: scrollback, entry line, history, worker-thread `ai` handling |
| `src/plugin/ValisProcessor.*` | AI endpoint, model and key storage with `ApplicationProperties` persistence |
| `src/ui/ValisEditor.*` | the Console tab and the three Settings items |
