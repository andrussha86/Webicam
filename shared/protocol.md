# Webicam protocol, version 6

Normative description of the wire protocol between the iPhone app and `webicam.exe`.
Both implementations (`windows/core/src/protocol.cpp`, `ios/WebicamCore`) must parse and
produce the test vectors in `testvectors/` byte for byte.

## Transport

TCP on port 47800 of the phone, reached two ways:

- **USB**: through the usbmux tunnel (Apple Mobile Device Service, `127.0.0.1:27015`); the phone sees a
  loopback peer.
- **Wi-Fi**: directly over the local network. The phone advertises the Bonjour service `_webicam._tcp`
  on port 47800 with a TXT record `id=<stable random id of this phone>`; the PC may also be given the
  address by hand. Any non-loopback peer is a Wi-Fi peer and must pair first (see Rules).

One client at a time — a new connection replaces the old one once it is accepted (a Wi-Fi peer only after
a correct Pair).

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
| Configure | 0x02 | PC → phone | `width: u16, height: u16, fps: u8, bitrate_kbps: u32` — the size of the frames sent; `width < height` is portrait (see Rules) |
| VideoFrame | 0x03 | phone → PC | `capture_time_us: u64` (phone monotonic clock), `flags: u8` (bit 0 = keyframe), then H.264 Annex B to the end of the payload. Keyframes carry SPS/PPS. |
| RequestKeyframe | 0x04 | PC → phone | empty |
| Ping | 0x05 | PC → phone | `pc_time_us: u64` |
| Pong | 0x06 | phone → PC | `pc_time_us: u64` (echoed), `phone_time_us: u64` |
| Status | 0x07 | phone → PC | `state: u8` (0 streaming, 1 stopped by user, 2 thermal throttled, 3 error, 4 pairing rejected), `thermal: u8` (0 nominal, 1 fair, 2 serious, 3 critical), `message: str` |
| CameraSettings | 0x08 | PC → phone | `lens: u8, zoom: f32, manual_exposure: bool, iso: f32, shutter_us: u32, exposure_bias: f32, manual_wb: bool, temperature_k: f32, tint: f32, manual_focus: bool, lens_position: f32, blur: u8, rotation: u8, face_focus: bool, background_mode: u8, background_color: u32, mirror_h: bool, mirror_v: bool, pan_x: f32, pan_y: f32` |
| CameraState | 0x09 | phone → PC | `lenses: u8` (bit i = lens i present), `lens: u8, iso_min: f32, iso_max: f32, shutter_min_us: u32, shutter_max_us: u32, bias_min: f32, bias_max: f32, zoom_max: f32, manual_focus_supported: bool, iso: f32, shutter_us: u32, temperature_k: f32, tint: f32, lens_position: f32, reactions_supported: bool, reactions_enabled: bool, gestures_enabled: bool, flipped: bool` |
| BackgroundImage | 0x0A | PC → phone | JPEG to the end of the payload; empty = no picture |
| Pair | 0x0B | PC → phone | `code: str` — the code shown on the phone's screen; Wi-Fi only |
| MicSettings | 0x0D | PC → phone | `enabled: bool` — send the microphone |
| AudioFrame | 0x0E | phone → PC | `capture_time_us: u64` (phone monotonic clock, first sample), then mono signed 16-bit little-endian samples at 48 000 Hz to the end of the payload; an odd sample byte count is a protocol error |
| Telemetry | 0x0C | phone → PC | `battery: u8` (percent 0…100, 255 unknown), `charging: u8` (0 unknown, 1 on battery, 2 charging, 3 full), `thermal: u8` (as in Status); other values are a protocol error |

Lenses: 0 ultra wide (0.5×), 1 wide (1×), 2 wide at 2× (48 MP crop), 3 telephoto, 4 front;
a lens value ≥ 5 is a protocol error. `zoom` ≥ 1 multiplies the lens's own zoom. `blur` is
0 (off) … 100. `lens_position` is 0 (near) … 1 (far). `exposure_bias` (EV) applies only with
auto exposure.

`background_mode`: 0 off, 1 blur (strength = `blur`, 0 treated as 1), 2 image, 3 colour; ≥ 4 is a
protocol error. `background_color` is `0x00RRGGBB`; a non-zero top byte is a protocol error.
`face_focus` turns on face-driven focus and exposure while those are on auto.

`rotation`: 0 auto, 1 upright, 2 turned by 180°; ≥ 3 is a protocol error. Auto follows how the phone
is held (gravity) but only ever chooses between upright and 180°, never between landscape and portrait.
`flipped` in CameraState says whether the picture is turned by 180° right now.

`mirror_h` mirrors the output left to right, `mirror_v` top to bottom (both = turned by 180°).

`zoom` is a crop of the output frame scaled back to its size; `pan_x`, `pan_y` are the centre of the
cropped area in output coordinates, 0…1 from the top left. The phone clamps them so the area stays
inside the frame; 0.5, 0.5 is the centre.

## Rules

- Wi-Fi pairing: the PC sends Hello and then Pair at once, without waiting for the phone. Until it gets a
  Pair with the right code the phone sends nothing, ignores Hello, and keeps any current client. A wrong
  code, any other message instead of Pair, or no Pair within 5 s gets one Status{pairing rejected, 0,
  reason} and the connection is closed. After a correct Pair the phone takes the connection as its client
  and sends its Hello. After a wrong code the phone refuses every Pair for 2 s without checking it.
  USB peers never pair; a Pair from them is ignored.
- The phone sends Telemetry right after its Hello and then every 2 s.
- MicSettings is a full snapshot, sent by the PC after every Configure (after CameraSettings) and whenever it
  changes. While it is enabled and the connection is up the phone sends AudioFrames of about 20 ms each,
  also while the video is paused. Without microphone permission the phone sends one
  Status{error, "Нет доступа к микрофону…"} instead.
- Both sides send Hello first. On a version mismatch the connection is closed and the UI
  says which side to update.
- Hello formats are the phone's landscape capture sizes (16:9 and native 4:3). A Configure may name one
  of them as is, or turned (`width < height`): the phone then turns the picture by 90° and sends
  portrait frames of exactly that size.
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
| `hello_pc.bin` | Hello{6} | `01 02 00 00 00 06 00` |
| `hello_phone.bin` | Hello{6, "iPhone16,1", [1280×720@60, 1920×1080@60, 1920×1440@60, 3840×2160@30]} | `01 24 00 00 00 06 00 0A 00 69 50 68 6F 6E 65 31 36 2C 31 04 00 00 05 D0 02 3C 80 07 38 04 3C 80 07 A0 05 3C 00 0F 70 08 1E` |
| `configure_1080p60.bin` | Configure{1920, 1080, 60, 25000} | `02 09 00 00 00 80 07 38 04 3C A8 61 00 00` |
| `video_frame_key.bin` | VideoFrame{123456789, key, `00 00 00 01 67 42 00 1F`} | `03 11 00 00 00 15 CD 5B 07 00 00 00 00 01 00 00 00 01 67 42 00 1F` |
| `request_keyframe.bin` | RequestKeyframe | `04 00 00 00 00` |
| `ping.bin` | Ping{1000} | `05 08 00 00 00 E8 03 00 00 00 00 00 00` |
| `pong.bin` | Pong{1000, 2000} | `06 10 00 00 00 E8 03 00 00 00 00 00 00 D0 07 00 00 00 00 00 00` |
| `status_thermal.bin` | Status{thermal throttled, 2, "serious"} | `07 0B 00 00 00 02 02 07 00 73 65 72 69 6F 75 73` |
| `camera_settings.bin` | CameraSettings{telephoto, 1.5, manual, 400, 8000, −0.5, manual, 4500, 10, manual, 0.25, 60, turned 180°, face, image, 0x336699, mirror_h, no mirror_v, 0.25, 0.75} | `08 32 00 00 00 03 00 00 C0 3F 01 00 00 C8 43 40 1F 00 00 00 00 00 BF 01 00 A0 8C 45 00 00 20 41 01 00 00 80 3E 3C 02 01 02 99 66 33 00 01 00 00 00 80 3E 00 00 40 3F` |
| `camera_state.bin` | CameraState{0x1F, wide, 50…3200, 20…16666 µs, −8…8, 10, focus, 200, 10000, 5200, −5, 0.75, reactions supported, enabled, no gestures, flipped} | `09 37 00 00 00 1F 01 00 00 48 42 00 00 48 45 14 00 00 00 1A 41 00 00 00 00 00 C1 00 00 00 41 00 00 20 41 01 00 00 48 43 10 27 00 00 00 80 A2 45 00 00 A0 C0 00 00 40 3F 01 01 00 01` |
| `background_image.bin` | BackgroundImage{`FF D8 FF`} | `0A 03 00 00 00 FF D8 FF` |
| `background_image_empty.bin` | BackgroundImage{} | `0A 00 00 00 00` |
| `pair.bin` | Pair{"482913"} | `0B 08 00 00 00 06 00 34 38 32 39 31 33` |
| `telemetry.bin` | Telemetry{87, charging, 1} | `0C 03 00 00 00 57 02 01` |
| `mic_settings.bin` | MicSettings{on} | `0D 01 00 00 00 01` |
| `audio_frame.bin` | AudioFrame{1000, [0, 1, −1, 32767, −32768]} | `0E 12 00 00 00 E8 03 00 00 00 00 00 00 00 00 01 00 FF FF FF 7F 00 80` |
