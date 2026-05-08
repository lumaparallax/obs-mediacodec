# obs-mediacodec

A lightweight hardware video encoding plugin for OBS Studio on Android/Termux. It uses the Android NDK `AMediaCodec` API to access the device's native hardware encoders for accelerated streaming and recording.

## Features
* **Hardware Encoding:** Supports H.264, HEVC, and AV1 encoding (depending on device hardware support) for both streaming and recording.
* **Low-Overhead Pipeline:** Uses `ANativeWindow` surface-based input for efficient frame transfer to the hardware encoder.
* **Compatibility Support:** Dynamically falls back to compatible H.264 profiles on older API 26 (Android 8) devices.
* **Encoder Controls:** Supports configurable bitrate and max bitrate settings.

## Requirements
* **OS:** Android / Termux (Minimum API 26 / Android 8.0)
* **OBS Studio:** Requires a Termux/Android-compatible build of OBS Studio.
* See the termux port here: [OBS-Studio Termux PR 29412](https://github.com/termux/termux-packages/pull/29412)

> **Note:** This plugin depends on Android's `libmediandk.so` and is Android-specific. It will not build for standard Linux, Windows, or macOS systems.

## Usage

1. Open OBS Studio.
2. Navigate to **Settings** > **Output**.
3. Set Output Mode to **Advanced**.
4. In the Streaming or Recording tab, select a `MediaCodec` encoder from the Video Encoder dropdown.

## Issues & Support

Due to differences between Android vendor encoder implementations, device-specific issues may occur depending on your hardware and firmware.

If you encounter encoder initialization failures or crashes, please open an issue and include:
1. Your device model and chipset.
2. Your Android version | Termux version.
3. The corresponding OBS Studio log file.
4. The full encoder name shown in OBS or a screenshot of the encoder dropdown (if the encoder appears or initializes successfully).

## License

Licensed under the GNU General Public License v2.0 or later.
