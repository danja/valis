# Console

The Console tab is a terminal into the loaded circuit. Local commands query
and drive it; an AI mode drafts new circuits for you.

## Commands

| Command | Effect |
|---|---|
| `help` | list commands |
| `stats` | element, arc and param counts plus latency |
| `turtle` | print the current Turtle source |
| `types [filter]` | element classes with ports, ranges and units |
| `params` | parameter bindings with live values |
| `get <slot>` / `set <slot> <val>` | read or move one parameter in its own units |
| `validate` | check the current Turtle without reinstalling it |
| `blocks` | Turtle blocks the AI proposed, with validation |
| `apply [n]` | install block `n` (default: most recent valid one) |
| `clear` | wipe the scrollback |

Up and Down browse input history.

## Asking the AI

`ai <prompt>` asks the model to design a circuit, for example
`ai a deep dub bass with slow filter sweep`. The reply prints inline. Turtle
code arrives in fenced blocks and each block is validated on arrival; install
one with `apply 0`. A block that fails validation prints its diagnostics and
`apply` refuses it. Installing a circuit behaves like any other edit: if it
will not compile, the previous circuit keeps playing.

Setup comes first: Settings menu, `AI Provider`, then `Set API Key...` (or
export the key; see below). Providers with a free tier and no card are
`Groq` (fast, model `openai/gpt-oss-20b`), `Gemini` (model
`gemini-3.5-flash`), and `OpenRouter` (model `openrouter/free`, which
auto-picks a free model, 50 requests a day). `Ollama (local)` needs no key
at all: install ollama and run `ollama pull llama3.1`. `Set AI Model...` and
`Set AI Endpoint...` still point the tab at anything OpenAI-compatible,
including a local server. The key also honours the `VALIS_MISTRAL_API_KEY`
environment variable when nothing is stored. Settings persist between
sessions on the local machine and are never saved into a DAW project, so the
key cannot leak through a shared session. The key belongs to the active
provider: switching provider keeps the old key, so set the new one next.

Only fenced code counts as a circuit: ```` ```turtle ```` blocks, or bare
```` ``` ```` blocks that mention `val:`. Everything else is conversation.
The model is instructed accordingly by a system prompt built from the
[circuit rules](circuits.md) and the [element reference](elements.md), so what
it proposes matches what the compiler accepts.

## When the AI refuses

An `HTTP 429` reply means the API is rate-limiting the key, not a problem in
the circuit. Free-tier keys allow very few requests per minute, so wait a
minute before retrying and avoid rapid repeats. If refusals persist, check
usage and limits at console.mistral.ai and outages at status.mistral.ai. An
`HTTP 401` reply means the key itself is wrong or missing: re-enter it under
Settings. Either way the loaded circuit keeps playing; only the request fails.

An `HTTP 404` naming the model means the provider retired that model id
(free-tier rosters change often). Pick the current one with Settings > Set AI
Model. For Groq, `curl https://api.groq.com/openai/v1/models -H
"Authorization: Bearer $KEY"` lists live ids. Re-selecting the provider
preset also restores its current default model.
