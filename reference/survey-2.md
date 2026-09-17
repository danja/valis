The survey strongly suggests that forcing every plugin through one scalar callback would be a mistake. Most cases naturally decompose into three bounded execution domains:

### 1. Sample kernel

text
Audio sample + state + audio-rate controls → Audio sample + new state


This covers IIRs, oscillators, envelope followers, waveshapers, circuit recurrences, delay taps, modulation and a surprisingly large amount of conventional DSP.

### 2. Block/window kernel

text
Frame[N] + block state → Frame[M] + new state


This is the natural home for FFT/STFT, convolution, reverse windows, some granular operations, neural inference, resampling and algorithms with explicit latency. It should not be disguised as thousands of scalar primitive nodes.

### 3. Event transition/scheduler

text
Timed Event + event state + transport → events / changes to bounded populations


This handles MIDI transformers, note allocation, sample triggering, voice stealing, grain scheduling, sequencers and transport-aware changes.

### 4. Immutable/resource plane

text
Resource<T> = identity + integrity + metadata + bounded runtime view


Samples, wavetables, impulse responses, tuning maps and neural weights should be first-class resources rather than unexplained bytes that happen to live in Wasm memory.
