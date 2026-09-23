# Sculpt Channel v0.4

This is a major DSP/UI revision.

## RES engine
The resonance stage is now a 32-band dynamic system instead of four broad detectors.

Band centres:
20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250,
315, 400, 500, 630, 800, 1k, 1.25k, 1.6k, 2k, 2.5k, 3k,
3.5k, 4k, 5k, 6.3k, 8k, 10k, 12.5k, 16k, 20k.

Each band compares:
- its current energy against neighbouring bands
- its current energy against its own slower temporal baseline

This lets RES react to both persistent spectral protrusions and short resonant events.

All 32 bands are controlled continuously by the four main macro knobs with overlapping
frequency weighting.

### VARIATION
Variation does not add another set of filters.
It increases detector sensitivity around:
200 Hz, 500 Hz, 1 kHz, 3 kHz, 5 kHz and 8 kHz,
including a smaller influence on adjacent bands.

## Macro behaviour
- much more progressive response from low knob values
- stronger positive-side compression
- negative side is substantially more reactive
- saturation starts later and is less dominant than v0.3
- processing is internally 4x oversampled
- compressor detection is stereo linked

## LEVEL MATCH
Optional slow level compensation for more meaningful A/B comparison.
It is intentionally slow so it does not behave like an audio compressor.

## GUI
Vintage hardware-style front panel:
- dark enamel / brushed faceplate
- wood side cheeks
- bakelite-style knobs
- brass collars and screws
- hardware toggle switches
- segmented RES / COMP / SAT meters
- 32-band RES activity matrix

The colour family remains amber / cyan / cream / charcoal.
