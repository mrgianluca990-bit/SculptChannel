# Sculpt Channel v0.6

Changes from v0.5:

- Level Match removed completely.
- Compression reduced significantly:
  - later onset
  - lower ratio
  - higher threshold
  - less gain reduction overall
- Saturation increased significantly:
  - earlier onset
  - stronger drive
  - stronger wet blend
  - second nonlinear stage at high values
- RES / VAR architecture rewritten:
  - 32 narrow bands are DETECTORS ONLY
  - no narrow band subtraction is performed in the audio path
  - detectors generate four smooth macro damping values
  - therefore VAR cannot create comb-filter behaviour
- VAR only increases detector sensitivity around:
  200 Hz, 500 Hz, 1 kHz, 3 kHz, 5 kHz, 8 kHz
- Maximum soothe action is intentionally moderate and broad.
- GUI simplified:
  - no Level Match control
  - no 32-band meter
  - dark rack/studio direction with red, amber, cream and cyan accents


## v0.6.3 GUI precision pass
- Control locations measured directly from the approved 1672x941 artwork instead of estimated.
- Exact knob centres: LOW 272/464, MID 578/459, HIGH 879/457, PRESENCE 1179/457, OUTPUT 1458/459.
- Meter LED coordinates measured from the source image pixels.
- Baked control faces/LEDs are removed from the background with inpainting before live controls are rendered.
- DSP unchanged from v0.6.
