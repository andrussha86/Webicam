# Webicam protocol, version 3

Normative description of the wire protocol between the iPhone app and `webicam.exe`.
Both implementations (`windows/core/src/protocol.cpp`, `ios/WebicamCore`) must parse and
produce the test vectors in `testvectors/` byte for byte.

## Transport

TCP. The phone listens on port 47800 and accepts only loopback peers; the PC reaches it through the usbmux tunnel
(Apple Mobile Device Service, `127.0.0.1:27015`). One client at a time — a new connection
replaces the old one.

## Framing

```
type: u8 | length: u32 | payload[length]
```

All integers are little-endian. `length` is the payload size and must be ≤ 16 777 216
(16 MiB); a larger value, an unknown type, or a payload that does not parse exactly is a
protocol error and closes the connection.

- `f32` = IEEE 754 single, little-endian; `bool` = `u8`, 0 or 1 (anything else is a protocol error)
- `str` = `length: u16` + UTF-8 bytes
- array = `count: u16` + elements

## Messages

| Type | Code | Direction | Payload |
|---|---|---|---|
| Hello | 0x01 | both, first message | `version: u16`; the phone appends `model: str`, `formats: [width u16, height u16, max_fps u8]`. The PC sends only `version` (payload length 2). |
| Configure | 0x02 | PC → phone | `width: u16, height: u16, fps: u8, bitrate_kbps: u32` |
| VideoFrame | 0x03 | phone → PC | `capture_time_us: u64` (phone monotonic clock), `flags: u8` (bit 0 = keyframe), then H.264 Annex B to the end of the payload. Keyframes carry SPS/PPS. |
| RequestKeyframe | 0x04 | PC → phone | empty |
| Ping | 0x05 | PC → phone | `pc_time_us: u64` |
| Pong | 0x06 | phone → PC | `pc_time_us: u64` (echoed), `phone_time_us: u64` |
| Status | 0x07 | phone → PC | `state: u8` (0 streaming, 1 stopped by user, 2 thermal throttled, 3 error), `thermal: u8` (0 nominal, 1 fair, 2 serious, 3 critical), `message: str` |
| CameraSettings | 0x08 | PC → phone | `lens: u8, zoom: f32, manual_exposure: bool, iso: f32, shutter_us: u32, exposure_bias: f32, manual_wb: bool, temperature_k: f32, tint: f32, manual_focus: bool, lens_position: f32, blur: u8, rotate180: bool, face_focus: bool, background_mode: u8, background_color: u32` |
| CameraState | 0x09 | phone → PC | `lenses: u8` (bit i = lens i present), `lens: u8, iso_min: f32, iso_max: f32, shutter_min_us: u32, shutter_max_us: u32, bias_min: f32, bias_max: f32, zoom_max: f32, manual_focus_supported: bool, iso: f32, shutter_us: u32, temperature_k: f32, tint: f32, lens_position: f32, reactions_supported: bool, reactions_enabled: bool, gestures_enabled: bool` |
| BackgroundImage | 0x0A | PC → phone | JPEG to the end of the payload; empty = no picture |

Lenses: 0 ultra wide (0.5×), 1 wide (1×), 2 wide at 2× (48 MP crop), 3 telephoto, 4 front;
a lens value ≥ 5 is a protocol error. `zoom` ≥ 1 multiplies the lens's own zoom. `blur` is
0 (off) … 100. `lens_position` is 0 (near) … 1 (far). `exposure_bias` (EV) applies only with
auto exposure.

`background_mode`: 0 off, 1 blur (strength = `blur`, 0 treated as 1), 2 image, 3 colour; ≥ 4 is a
protocol error. `background_color` is `0x00RRGGBB`; a non-zero top byte is a protocol error.
`face_focus` turns on face-driven focus and exposure while those are on auto.

## Rules

- Both sides send Hello first. On a version mismatch the connection is closed and the UI
  says which side to update.
- Streaming starts only after Configure. Each Configure reconfigures capture and encoder; the
  first frame after it is a keyframe. After each Configure the phone sends a Status with its
  actual state and a CameraState.
- CameraSettings is a full snapshot: the PC sends it after every Configure and whenever the
  user changes something (at most 20 per second). The phone clamps each value to the active
  lens; a lens that cannot run the current format is refused with Status{error} and the
  previous lens stays.
- BackgroundImage is the last picture received; the phone keeps it until the connection closes.
  The PC sends it after every Configure and before CameraSettings when `background_mode = 2`, and
  whenever the picture changes. The PC scales it to cover the stream resolution exactly.
  A missing or undecodable picture makes the phone send frames untouched and one
  Status{error, "Не удалось открыть картинку фона"} per picture.
- The phone sends CameraState after Configure, after CameraSettings and every 0.5 s.
- The PC sends Ping once per second. The clock offset is taken from the sample with the
  smallest round trip among the last 10: `offset = (pc_send + pc_recv) / 2 − phone_time`, and
  `pc_time ≈ phone_time + offset`.

## Test vectors

| File | Value | Bytes |
|---|---|---|
| `hello_pc.bin` | Hello{3} | `01 02 00 00 00 03 00` |
| `hello_phone.bin` | Hello{3, "iPhone16,1", [1280×720@60, 1920×1080@60, 3840×2160@30]} | `01 1F 00 00 00 03 00 0A 00 69 50 68 6F 6E 65 31 36 2C 31 03 00 00 05 D0 02 3C 80 07 38 04 3C 00 0F 70 08 1E` |
| `configure_1080p60.bin` | Configure{1920, 1080, 60, 25000} | `02 09 00 00 00 80 07 38 04 3C A8 61 00 00` |
| `video_frame_key.bin` | VideoFrame{123456789, key, `00 00 00 01 67 42 00 1F`} | `03 11 00 00 00 15 CD 5B 07 00 00 00 00 01 00 00 00 01 67 42 00 1F` |
| `request_keyframe.bin` | RequestKeyframe | `04 00 00 00 00` |
| `ping.bin` | Ping{1000} | `05 08 00 00 00 E8 03 00 00 00 00 00 00` |
| `pong.bin` | Pong{1000, 2000} | `06 10 00 00 00 E8 03 00 00 00 00 00 00 D0 07 00 00 00 00 00 00` |
| `status_thermal.bin` | Status{thermal throttled, 2, "serious"} | `07 0B 00 00 00 02 02 07 00 73 65 72 69 6F 75 73` |
| `camera_settings.bin` | CameraSettings{telephoto, 1.5, manual, 400, 8000, −0.5, manual, 4500, 10, manual, 0.25, 60, rotate, face, image, 0x336699} | `08 28 00 00 00 03 00 00 C0 3F 01 00 00 C8 43 40 1F 00 00 00 00 00 BF 01 00 A0 8C 45 00 00 20 41 01 00 00 80 3E 3C 01 01 02 99 66 33 00` |
| `camera_state.bin` | CameraState{0x1F, wide, 50…3200, 20…16666 µs, −8…8, 10, focus, 200, 10000, 5200, −5, 0.75, reactions supported, enabled, no gestures} | `09 36 00 00 00 1F 01 00 00 48 42 00 00 48 45 14 00 00 00 1A 41 00 00 00 00 00 C1 00 00 00 41 00 00 20 41 01 00 00 48 43 10 27 00 00 00 80 A2 45 00 00 A0 C0 00 00 40 3F 01 01 00` |
| `background_image.bin` | BackgroundImage{`FF D8 FF`} | `0A 03 00 00 00 FF D8 FF` |
| `background_image_empty.bin` | BackgroundImage{} | `0A 00 00 00 00` |
