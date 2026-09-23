# Sculpt Channel v0.5

## Core philosophy

v0.5 returns the plugin to the intended musical behaviour:

**EQ -> COMP -> SAT**

The four macro controls now behave like broad EQ bands being pushed into a compressor
and then into an analogue-style nonlinear stage.

### Positive values
- first: broad EQ boost becomes audible
- then: compression becomes increasingly obvious
- finally: the band starts to distort/saturate

The saturation begins after the compressor is already working.

### Negative values
- broad EQ cut is obvious
- a lighter amount of compression remains active
- saturation is almost absent

## 32-band Soothe guardrail

The 32-band engine still exists, but it is no longer the protagonist.

It runs after EQ -> COMP -> SAT and only reacts when a narrow area becomes clearly
more resonant than its neighbours or than its own slow temporal baseline.

Normal maximum reduction is intentionally small, roughly in the 0.5–3 dB region.

### VARIATION
Variation increases sensitivity around:
- 200 Hz
- 500 Hz
- 1 kHz
- 3 kHz
- 5 kHz
- 8 kHz

Those focused areas can reach stronger correction, up to roughly 4–5 dB when necessary.

## GUI
- removed the 32-band activity display
- kept only useful RES / COMP / SAT meters per macro
- vintage dark enamel / bakelite / brass / wood visual language
- Variation and Level Match remain hardware-style toggle switches

## Processing
- internally 4x oversampled
- stereo-linked compressor detection
- optional slow Level Match
