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
including a local server. A key that is not stored is looked up in the
provider's own environment variable (`MISTRAL_API_KEY`, `GROQ_API_KEY`,
`GEMINI_API_KEY`, `OPENROUTER_API_KEY`), then in `VALIS_MISTRAL_API_KEY`,
which earlier versions used for one shared key. Settings persist between
sessions on the local machine and are never saved into a DAW project, so the
key cannot leak through a shared session.

Each key belongs to one provider and is stored separately, so several can be
configured at once. That is worth doing: when the chosen provider will not
serve a request, the console tries the others in turn rather than failing.

Only fenced code counts as a circuit: ```` ```turtle ```` blocks, or bare
```` ``` ```` blocks that mention `val:`. Everything else is conversation.
The model is instructed accordingly by a system prompt built from the
[circuit rules](circuits.md) and the [element reference](elements.md), so what
it proposes matches what the compiler accepts.

## Big circuits and rate limits

Every request carries the loaded circuit, so the circuit's size decides what
can be asked. A free tier's limit is a ceiling on one request rather than a
queue that is reached eventually: `examples/909.ttl` is about 17,000 tokens
of Turtle against Groq's 8,000 per minute, so on Groq that circuit cannot be
sent whole however long you wait. Mistral's budget is far larger and takes it
without complaint.

The console handles this for you, and says what it did:

- It measures each provider's budget from the headers of whatever that
  provider last replied, failures included.
- It refuses a request that is larger than a known budget before sending it,
  naming both numbers, instead of spending a round-trip to be refused.
- It cuts the prompt down when the budget is tight: first the element
  catalogue, then the circuit itself, which is sent as the part that relates
  to the request rather than in full. When that happens the console says so,
  and the model's Turtle is then an **edit** to paste into the Code tab, not
  a complete circuit to `apply`.
- It tries the other configured providers when the chosen one will not serve
  the request, and prints one line per provider it skipped and why. A
  malformed request is never retried elsewhere: every provider would say the
  same thing.

## When the AI refuses

An `HTTP 429` reply means the provider is rate-limiting the key, not a
problem in the circuit, and the reply text says which budget was hit. A
requests-per-minute limit is worth waiting out: free-tier keys allow very
few requests per minute, so wait a minute before retrying and avoid rapid
repeats. A tokens-per-minute (TPM) limit is different, and an `HTTP 413`
means the same thing: the request as a whole is larger than the budget, so
waiting cannot help. Shorten the request, work on a smaller circuit, or
switch to a provider or model with a higher limit under Settings > AI
Provider. Either way, check the current provider's own usage dashboard for
its limits and outage status - the URL differs per provider.
An `HTTP 401` reply means the key itself is wrong or missing: re-enter it
under Settings. An `HTTP 402` means the account behind the key cannot be
billed; the console stops asking that provider until the key changes.
Whatever the failure, the loaded circuit keeps playing; only the request
fails.

An `HTTP 404` naming the model means the provider retired that model id
(free-tier rosters change often). Pick the current one with Settings > Set AI
Model. For Groq, `curl https://api.groq.com/openai/v1/models -H
"Authorization: Bearer $KEY"` lists live ids. Re-selecting the provider
preset also restores its current default model.
