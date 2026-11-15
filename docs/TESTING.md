# Testing RF Tools

## Decoder regression

Requirements: Python 3 and a host C compiler named `gcc` available on PATH, plus the matching firmware source tree.

```sh
python tests/test_princeton_snapshot.py --firmware-root /path/to/unleashed-firmware-Nhscan
```

The test extracts and compiles the actual Princeton `feed` function from that firmware tree with minimal host stubs. It generates frames for five TE values, checks the callback data, and proves that the live decoder's timing resets and changes while a callback-time copy remains stable. This isolates decoder timing lifetime; it does not emulate the radio, RTOS, complete RF Tools engine, or physical receiver.

## Device checklist

- Record hardware, firmware version, app version, radio selection, frequency, and preset.
- Capture the same known remote repeatedly in Read and compare timing to stock Read.
- Check manual Send and automatic Repeater operation with the intended receiver.
- Verify countdown and Sending text do not overlap the displayed capture details.
- Cancel during the countdown, a burst, and a long repeat delay.
- Record RAW longer than two seconds and check the full recording transmits.
- Save a decoded file, transmit it, and verify its contents remain unchanged.
- Test internal and external radio selection and fallback when no external device is present.
- Confirm invalid-preset files fail instead of silently transmitting with another preset.

Keep the configured frequency correct before selecting a Saved Signals file: selection immediately transmits using the current app frequency.
