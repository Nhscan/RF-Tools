# Changelog

## 1.1 — 2026-11-14

Initial public app release with decoded-transmission fixes.

### Fixed

- Snapshot the complete decoded signal inside the receiver callback, following stock history behavior. Previously RF Tools serialized a pointer to a live decoder later, after timing fields had been reset and reused. The key could remain correct while Princeton TE became incorrect.
- Use the snapshot for decoded Save, Read Send, and Repeater Auto-TX.
- Give encoders a mutable copy so transmitting does not alter the capture snapshot or original saved decoded file.
- Separate repeater details, countdown, and cancellation hint into distinct screen areas.
- Ignore OK during pending or active automatic transmission.
- Reset the cancellation flag before starting the TX thread, avoiding a lost early Back press.
- Check cancellation during repeat delays instead of blocking for the full gap.
- Handle countdown tick wraparound.
- Reject an invalid or missing preset when transmitting a file.
- Reset custom-button state before transmission and new captures.
- Increase per-burst TX timeout from two seconds to two minutes for longer RAW recordings.

### Included features

Decoded Read, explicit RAW recording, capture saving, SD-card signal browsing, automatic decoded repeater, internal/external CC1101 selection, adjustable repeat/delay, and frequency analyzer.

### Validation

- `fap_rf_tools` build and API 88.27 app validation passed.
- Regression harness using the actual Princeton feed function passed for five timing values from 350–390 microseconds, verifying snapshot stability after decoder reset and subsequent edges.
- Maintainer reported the fix worked on their device. Broader hardware and protocol coverage is not claimed.
