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

The prompt carries the whole circuit, so how big the circuit is decides whether
the request can be sent at all. See [Rate limits](#rate-limits) below.

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
local `ApplicationProperties` between sessions.

Keys are stored per provider, so more than one can be configured at once and the
console can move between them. A key that is not stored is looked up in the
provider's own environment variable (`MISTRAL_API_KEY`, `GROQ_API_KEY`,
`GEMINI_API_KEY`, `OPENROUTER_API_KEY`), and then in `VALIS_MISTRAL_API_KEY`,
which earlier versions used for a single shared key. No key is ever written to
DAW state, so a saved project cannot leak one.

## Rate limits

A free tier's limit is not a queue that is reached eventually. It is a ceiling
on a single request: a prompt larger than the whole per-minute token budget can
never be sent to that provider, however long the console waits. `examples/909.ttl`
is roughly 17,000 tokens of Turtle against Groq's measured 8,000 per minute, so
it is unsendable there whatever else happens. The measurements behind this are in
[docs/rate-limit-advice.md](rate-limit-advice.md).

Four things follow from that, and the console does all four.

**It reads the budget rather than guessing it.** `curl -D` captures the response
headers, which state the limit, the remaining count and the reset on every
response including the failures. Header names differ per provider, so each entry
in `AiProviders` carries its own names: Groq says `x-ratelimit-limit-tokens`,
Mistral says `x-ratelimit-limit-tokens-minute` and sends no reset at all,
OpenRouter and Gemini send nothing. Nothing measured stays distinguishable from
a measured zero, so an unprobed provider never looks exhausted.

**It refuses an impossible request locally.** Once a provider's limit is known, a
prompt larger than the whole budget is refused without a round-trip, with a
message naming both numbers. The estimate is a character count at 0.42 tokens
per character, which is what Turtle measures; the familiar "characters over four"
underestimates a circuit by about forty per cent, in the direction that causes
failures. Each reply's `usage.prompt_tokens` corrects the estimate afterwards.

**It sends less when less will fit.** The element catalogue is cut back to the
classes the circuit and the request actually use, and then, if that is not
enough, the circuit itself is sent as a subgraph: the elements the request names,
the arcs that touch them, and what those arcs reach, with a line naming how many
elements were left out and of what kind. The reply is then an edit rather than a
complete circuit, and the console says so, because a block redefining six of
seventy-eight elements is not something to install whole.

**It moves on rather than waiting.** The provider list is an order. A request the
chosen provider will not serve falls through it, and the classification is what
makes that safe:

| Status | Meaning | Response |
|---|---|---|
| 429 | rate limit | wait if the window rolls soon, otherwise try another provider |
| 413 | too large for this provider | try a bigger provider now; waiting cannot help |
| 401, 402, 403 | this provider will not serve us | try another, and stop asking this one this session |
| 408, 5xx | provider fault | try another |
| 400 | our request is wrong | do **not** try another: every provider will say the same |

Waiting happens at most once per request and only for a window that rolls within
twenty seconds, because a tight retry loop against a free tier is how an account
earns a longer cooldown.

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
| `include/valis/Console.h`, `src/console/Console.cpp` | `ConsoleSession`: commands, extraction, validation. In `valis_core`, no UI dependency, covered by `tests/console/ConsoleTest.cpp` |
| `src/console/AiPrompt.cpp` | the same class's prompt building: system prompt, user message, and the trimming that fits both to a budget |
| `src/ai/ChatClient.h/.cpp` | request builder, reply parser, curl transport with injectable stub. Plugin target only, covered by `tests/ai/ChatClientTest.cpp` |
| `include/valis/RateLimits.h`, `src/ai/RateLimits.cpp` | token estimate, status classification, response-header parsing. Pure, in `valis_core` because the console sizes prompts with it; covered by `tests/ai/RateLimitsTest.cpp` |
| `include/valis/ProviderBudgets.h`, `src/ai/ProviderBudgets.cpp` | what each provider reported this session, and whether a request can be afforded |
| `src/ai/AiRouter.h/.cpp` | the provider order and the fall-through, covered by `tests/ai/AiRouterTest.cpp` |
| `src/ui/ConsoleView.h/.cpp` | the tab: scrollback, entry line, history, worker-thread `ai` handling |
| `src/plugin/ValisProcessor.*` | AI endpoint, model and key storage with `ApplicationProperties` persistence |
| `src/ui/ValisEditor.*` | the Console tab and the three Settings items |
