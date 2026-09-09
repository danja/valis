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

Setup comes first: Settings menu, `Set Mistral API Key...` (or export
`VALIS_MISTRAL_API_KEY`). The default endpoint is the Mistral API with model
`mistral-small-latest`, which has a free tier. `Set AI Model...` and `Set AI
Endpoint...` point the tab at anything OpenAI-compatible, including a local
server. Settings persist between sessions on the local machine and are never
saved into a DAW project, so the key cannot leak through a shared session.

Only fenced code counts as a circuit: ```` ```turtle ```` blocks, or bare
```` ``` ```` blocks that mention `val:`. Everything else is conversation.
The model is instructed accordingly by a system prompt built from the
[circuit rules](circuits.md) and the [element reference](elements.md), so what
it proposes matches what the compiler accepts.
