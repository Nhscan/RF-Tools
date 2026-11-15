# RF Tools for Flipper Zero

Read, record, save, transmit, repeat decoded signals, and analyze frequencies in one Sub-GHz app. Developed by **Nhscan** against the [Nhscan Unleashed firmware fork](https://github.com/Nhscan/unleashed-firmware-Nhscan).

**Current app version: 1.1.** The included release build targets **f7, firmware API 88.27**. It is a firmware-specific app, not a universal binary for every Flipper firmware.

## Features

| Menu | Current behavior |
| --- | --- |
| Capture → Read | Decodes a supported signal, preserves its complete data immediately, and offers Send and Save. |
| Capture → Read Raw | Explicit Record → Stop → Send/Save workflow for raw pulse recordings. |
| Repeater → Listen | Listens for a decoded signal. With Auto-TX enabled, shows a countdown, transmits, then resumes listening. |
| Repeater → Settings | Repeat count, delay, saving mode, TX As, and Auto-TX controls. See the limitations below for TX As. |
| Frequency Analyzer | Scans configured frequencies, shows signal strength and recent detections, and lets OK open Read on the selected frequency. |
| Saved Signals | Browses `.sub` files across the SD card. Selecting a file immediately starts transmission. |
| Radio Settings | Internal/external CC1101 selection, frequency, preset, repeat count, and delay. |

Both decoded and RAW transmission use the selected radio. External selection falls back to the internal radio if the external device is unavailable.

## Install

1. Get `rf_tools.fap` from this repository's **Releases** section.
2. Connect your device to qFlipper and open its SD card file manager.
3. Copy `rf_tools.fap` into **SD Card → apps → Sub-GHz**. Close an older running RF Tools instance before replacing it.
4. On the device, open **Applications → Sub-GHz → RF-Tools**.
5. Set your radio, frequency, and preset in **Radio Settings** before testing.

If the app reports an API mismatch, rebuild against your installed firmware's source. Do not assume a binary built for another firmware is compatible.

The maintainer confirmed the 1.1 fix worked on their connected device. The development handoff identifies that setup as a custom T-Embed CC1101 Plus build with internal/external radio support. Broad stock-Flipper and other-firmware testing remains open; please report your hardware and firmware when testing.

## Quick start

### Capture a decoded remote

Choose **Capture → Read**, press the remote's button, then select **Send** to test or **Save** to name the capture. Saving returns to the main menu. Capture uses the configured frequency; it does not frequency-hop.

### Record RAW

Choose **Capture → Read Raw**, press **Record**, operate your remote, and press **Stop**. You can then send or save the recording. RAW recording does not decode in parallel. Keep recordings below the current two-minute per-burst TX timeout.

### Use the repeater

1. In **Radio Settings**, choose the radio, frequency, and preset.
2. Open **Repeater → Settings** and set **Repeat**, **Delay**, and **Auto-TX: On**.
3. Open **Listen** and transmit your test signal.
4. RF Tools displays the decoded result and countdown, sends the configured bursts, and resumes listening.
5. **Back** cancels a pending transmission or requests cancellation during transmission.

The default is **3 bursts** with a **1-second delay**. Repeat ranges from **1–10** and Delay from **1–30 seconds**. The delay applies both before automatic transmission and between bursts. These settings are shared with manual Send and Saved Signals. Each decoded burst currently requests 10 internal protocol repetitions; that is separate from the burst count.

With Auto-TX off, Listen opens the normal result screen for manual Send/Save. Auto-save applies to the automatic repeater cycle. Manual captures still use the Save button.

## Files and settings

- Saved captures: `/ext/subghz/RF-Tools/`
- Settings: `/ext/apps_data/rf_tools/rf_tools.conf`
- RAW recording uses a temporary `.sub` file until saved.
- Saved decoded files are copied into memory for transmission so encoder edits do not change the original file.

## Important current limitations

- **Saved Signals transmits immediately after selection**, using the app's currently configured frequency and the file's preset. Set the frequency to match the signal before selecting a file.
- **Repeater listens for decoded signals only.** The existing TX As Raw option does not provide raw capture/replay in Repeater; use Capture → Read Raw for RAW recordings.
- Protocol reception does not guarantee transmission support. The firmware must provide a compatible encoder.
- Receiving a rolling-code signal does not mean replaying it will operate its receiver.
- The analyzer uses a generic preset and has coarser fine-frequency resolution than the stock analyzer's register-tuned implementation.
- Long transmissions have a two-minute timeout per burst. Cancellation remains available.

## Build from source

This repository contains the app source. It needs a compatible complete firmware tree and its toolchain.

```sh
git clone --recursive https://github.com/Nhscan/unleashed-firmware-Nhscan.git
git clone https://github.com/Nhscan/RF-Tools.git
```

Copy this repository's `applications/system/rf_tools` directory into the same relative location in the firmware checkout, then run from the firmware root:

```sh
./fbt fap_rf_tools
./fbt launch APPSRC=rf_tools
```

On Windows PowerShell:

```powershell
.\fbt.cmd fap_rf_tools
.\fbt.cmd launch APPSRC=rf_tools
```

The validated build used firmware commit `32b0bf4069ffd72d533323bb49db7ea374f0109b`, API **88.27**. To reproduce that baseline, check out that commit and run `git submodule update --init --recursive` before building. Later firmware versions may require changes.

The binary is generated under `build/f7-firmware-DC/.extapps/rf_tools.fap` for the validated configuration. Other configurations may use a different build directory.

## Testing and reports

See [TESTING.md](docs/TESTING.md) for the decoder regression test and a device checklist. Use GitHub Issues and include app version, firmware version, radio selection, protocol, frequency, expected behavior, and actual behavior. Do not upload private remote keys or captures you do not want publicly available.

See [CHANGELOG.md](CHANGELOG.md) for changes and [NOTICE.md](NOTICE.md) for credits. Source is distributed under the repository's [GNU GPL v3 license](LICENSE).
