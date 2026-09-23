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


## v0.6.1 GUI implementation
- GUI now uses the approved mockup image as the real plugin background.
- Added BinaryData asset embedding (`assets/sculpt_gui_bg.png`).
- Dynamic controls are overlaid directly on the artwork: 4 macro knobs, output, VAR, IN/OUT meters, and RES/COMP/SAT meters.
- Level Match remains removed.
