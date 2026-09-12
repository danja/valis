# Rate limits: notes from building peasant

Peasant (`/chalet/github/peasant`) is a coding harness built against the same
free tiers Valis uses: Groq, Mistral, OpenRouter, Gemini. It ran into every
problem below and the fixes are working code. Every figure here was measured on
2026-09-12 against live endpoints, not quoted from documentation, because the
published figures turned out to be wrong in both directions.

Valis is further along than peasant was. `formatHttpError` already splits a 429
into a request limit and a token limit, which peasant did not do at all, and
`AiProviders` is already a list of endpoints. Most of what follows builds on
what is there.

## 1. The number that decides everything

Measured with peasant's estimator against `examples/`:

| Circuit | chars | tokens |
|---|---|---|
| `basic.ttl` | 1,646 | ~840 |
| `clarinet.ttl` | 3,454 | ~892 |
| `modulation.ttl` | 3,328 | ~1,698 |
| `klon.ttl` | 4,549 | ~2,320 |
| `909.ttl` | 39,663 | **~20,229** |

Groq's free tier allows **8,000 tokens per minute**. `909.ttl` on its own is two
and a half times that. No amount of waiting will ever send it, because the limit
is not a queue you reach the front of, it is a ceiling on a single request.

That distinction is the most important thing in this document. Two failures look
alike and need opposite responses:

- **No room right now.** Waiting helps. The window rolls and the request fits.
- **Bigger than the whole budget.** Waiting does nothing, forever. The request
  must get smaller or go somewhere else.

`vocabs/valis.ttl` is 50,695 characters, roughly 26,000 tokens. If it is ever
included in a system prompt, every request is unsendable on Groq before the
circuit is added.

**Turtle is code, not prose.** Peasant measured 0.214 tokens per character for
English and **0.425 for code**, consistent to within two per cent across sample
sizes. The common estimate of "characters divided by four" (0.25) underestimates
a Turtle document by about forty per cent, in the direction that causes
failures. Use 0.42 for `.ttl`, and treat the result as a floor rather than a
guess.

## 2. The failure you have not handled yet

`formatHttpError` covers 429, 401, 403 and 5xx. It has no branch for **413**,
and Groq answers a per-minute token overflow with 413 rather than 429:

```
HTTP 413
retry-after: 31
x-ratelimit-limit-tokens: 8000
x-ratelimit-remaining-tokens: 8000
Request too large for model `openai/gpt-oss-20b` in organization ... on tokens
per minute (TPM): Limit 8000, Requested 13266
```

So a user loading `909.ttl` and asking a question gets "HTTP 413: the API
refused the request" with no advice, when this is exactly the case your 429
token branch already explains well. Route 413 to that same message.

Note `x-ratelimit-remaining-tokens: 8000`. The budget is **full**. Nothing was
consumed because nothing was sent. This is a size refusal, not a rate refusal,
and treating it as a rate refusal produces advice that cannot work.

Peasant classified 413 as a malformed request, which meant it refused to try a
different provider and surfaced the error instead. That was the single worst bug
in the project, and it was invisible until it happened to a real user.

## 3. You are discarding the answer

`curlPost` passes `-w "\nVALIS_HTTP_CODE:%{http_code}\n"`, which captures the
status and nothing else. The response headers carry the actual budget, and they
arrive on every response including the failures:

**Groq**
```
x-ratelimit-limit-tokens: 8000        x-ratelimit-reset-tokens: 644ms
x-ratelimit-remaining-tokens: 7914    x-ratelimit-reset-requests: 1m26.4s
x-ratelimit-limit-requests: 1000
```

**Mistral**
```
x-ratelimit-limit-tokens-minute: 625000    x-ratelimit-tokens-query-cost: 12
x-ratelimit-remaining-tokens-minute: 624988
x-ratelimit-limit-req-minute: 125
```

Adding `-D -` to the curl arguments, or `-D <tempfile>`, costs one flag and
turns guessing into measurement. Three things to know before parsing them:

- **The names differ per provider.** Groq says `-tokens`, Mistral says
  `-tokens-minute`. A map per provider entry in `AiProviders` is the natural
  place for this, alongside `endpoint` and `model`.
- **Groq's reset is a duration string**, `644ms` or `1m26.4s`, not a number of
  seconds and not a timestamp. A regular expression that matches `1m` out of
  `1m26.4s` will wait twenty six seconds too little, every time.
- **Mistral sends no reset at all.** The window is stated only in the header
  name. Its budget has to be inferred and therefore held more conservatively
  than Groq's, despite being seventy eight times larger.
- **OpenRouter and Gemini send nothing.** For those, a 429 or 413 is the only
  signal there will ever be. Unknown has to stay distinguishable from zero, or
  an unprobed provider looks exhausted.

## 4. Refuse before sending

Once a limit is known, a request larger than the whole budget can be refused
locally, instantly, with a message that tells the user something true:

> This circuit is about 20,000 tokens. Groq's limit is 8,000 per minute. Switch
> to Mistral under Settings, or send a smaller circuit.

That is better than a round trip, a 413, and a message about waiting. Peasant
learns the limit from the first refusal, since the headers arrive on the 413
itself, and never sends an impossible request to that provider again.

One subtlety that cost peasant a bug. Groq reports `reset-tokens: 1ms` on the
very response that states the limit, so by the time anything checks, the window
has rolled. A rolled window restores the *remaining* budget, it does not make
the *limit* unknown. Forgetting the limit meant sending the same impossible
request over and over.

## 5. Rotation is the real fix, and you are most of the way there

`AiProviders` is already a list. The change is to treat it as an *order* rather
than a menu of one, and move on when the current provider will not serve.

The measured headroom makes the case on its own:

| Provider | tokens per minute |
|---|---|
| Groq | 8,000 |
| Mistral | **625,000** |

Seventy eight times. A circuit that Groq can never accept is unremarkable to
Mistral. For Valis specifically, Mistral first is almost certainly right: the
prompts here carry whole Turtle documents, and speed matters less than whether
the request goes at all.

What matters is classifying the failure correctly, because the wrong call is
expensive in both directions. Peasant's table, arrived at by getting it wrong
twice against real providers:

| Status | Meaning | Response |
|---|---|---|
| 429 | rate limit | wait if the window rolls soon, otherwise try another provider |
| **413** | too large for this provider | try a bigger provider now. Waiting cannot help |
| 401, 402, 403 | this provider will not serve us | try another, and stop asking this one this session |
| 408, 5xx | provider fault | try another |
| 400 | our request is wrong | do **not** try another. Every provider will say the same |

Cerebras returns **402 Payment required** on every completion while answering
`/models` normally. Peasant classified that as a bad request, so one account's
billing problem took down every session. The distinction between "our request is
wrong" and "this provider will not serve us" is the one to get right.

## 6. When the circuit itself is too big

Rotation does not help if the content exceeds every provider. Three things that
do, cheapest first:

- **Do not send the vocabulary.** 26,000 tokens of `valis.ttl` in a system
  prompt is unsendable on most free tiers before the user has typed anything.
  Send the class and property names the question needs, not the ontology.
- **Send the relevant subgraph, not the file.** Valis already has a model and a
  compiler that know which elements and arcs relate to each other. A question
  about a filter does not need the drum voices. This is the largest saving
  available and the one Valis is uniquely well placed to make, because the
  structure is already parsed and queryable rather than being a wall of text.
- **Summarise the rest.** "Forty two other elements, mostly percussion voices"
  costs a dozen tokens and keeps the model from assuming the circuit is what it
  can see.

Peasant's equivalent is truncating tool results head and tail and reading files
in windows. The principle is the same: never send a whole document when the
question is about part of it.

## 7. Backing off without making it worse

- Honour `retry-after` when it is there. Groq sends it on 413 as well as 429.
- When it is absent, wait long enough to matter. Mistral's 429 has an **empty
  body and no `retry-after`**, so a local default is all there is, and twenty
  seconds is a reasonable one. A tight retry loop against a free tier is how an
  account earns a longer cooldown.
- A 429 is authoritative and overrides anything the headers implied. Peasant
  once saw Mistral return `x-ratelimit-limit-req-minute: 0`, having reported 125
  a few hours earlier. Treat a limit of zero as a momentary symptom rather than
  a durable fact, or one odd response removes the provider for the session.

## 8. Not worth doing

- **Counting tokens exactly.** No tokeniser is worth the dependency, every
  provider tokenises differently, and an estimate with a known error and a safe
  bias is enough. Peasant estimates from character counts and then corrects
  itself against `usage.prompt_tokens` in each reply. It settled within ten per
  cent after sixteen responses.
- **Trusting published limits.** Every one peasant checked was wrong. Groq
  documents 6,000 tokens per minute and reports 8,000. Mistral is described as
  "roughly one request per second" and reports 125 per minute and 625,000 tokens
  per minute. Read the headers.
- **Caching responses.** Suggested by a model when asked about this, and wrong
  here. Two questions about a circuit are rarely identical, and the cost is in
  the prompt rather than the reply.

## Summary, in order of value for the effort

1. Add a 413 branch to `formatHttpError`, routed to the existing token-limit
   message. Half an hour, and it fixes the confusing failure users hit first.
2. Capture response headers in `curlPost` with `-D`, and store the limit and
   remaining count per provider. One curl flag and a small parser.
3. Refuse a request larger than the known limit locally, with a message naming
   the number and the alternative.
4. Send the relevant subgraph rather than the whole Turtle document.
5. Make `AiProviders` an order and fall through it on 413, 429, 402 and 5xx, but
   never on 400.

Peasant's implementations, if they are useful as reference rather than as
something to copy:

- `src/provider/RateLimiter.js` - headers to budget, including the duration
  parser and the zero-limit case
- `src/provider/Router.js` - rotation and failure classification
- `src/provider/OpenAICompatClient.js` - `classify()`, the status table above
- `src/agent/TokenEstimator.js` - estimation and self-correction
- `docs/providers.md` - every measurement, with the raw responses behind it
