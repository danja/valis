I'm not hearing anything from the  Bass Drum in 909.ttl, I suspect it's a midi routing issue

Add the extra elements necessary to get a more faithful reproduction : more oscillators on the hi-hats, plus choke. Anything else that has been  simplified.

Can you add Pan and Level to each of the instruments in 909.ttl

Maximise compatibility with /home/danny/github/transmission - try it as a test host using MCP. We maintain Transmission so can change things over there if need be.

I don't know what we have already, but a test fixture that can examine the actual signal behaviour of an individual element would be useful. Then each element could have its own test using the fixture for sanity-checking levels etc.

A signal generator, an oscilloscope and a freq analysis meter elements could be useful during circuit development. (Some of the code could be shared with the test fixture).

## Recurring - check periodically

* remove tasks that have been done from this file
* for new material, check test coverage
* ensure README.md and docs are up-to-date
