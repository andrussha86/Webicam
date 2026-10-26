# Webicam protocol, version 1

Normative description of the wire protocol between the iPhone app and `webicam.exe`.
Both implementations (`windows/core/src/protocol.cpp`, `ios/WebicamCore`) must parse and
produce the test vectors in `testvectors/` byte for byte.

## Transport

TCP. The phone listens on `127.0.0.1:47800`; the PC reaches it through the usbmux tunnel
(Apple Mobile Device Service, `127.0.0.1:27015`). One client at a time — a new connection
replaces the old one.

## Framing

```
type: u8 | length: u32 | payload[length]
```

All integers are little-endian. `length` is the payload size and must be ≤ 16 777 216
(16 MiB); a larger value, an unknown type, or a payload that does not parse exactly is a
protocol error and closes the connection.

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

## Rules

- Both sides send Hello first. On a version mismatch the connection is closed and the UI
  says which side to update.
- Streaming starts only after Configure. Each Configure reconfigures capture and encoder; the
  first frame after it is a keyframe.
- The PC sends Ping once per second. The clock offset is taken from the sample with the
  smallest round trip among the last 10: `offset = (pc_send + pc_recv) / 2 − phone_time`, and
  `pc_time ≈ phone_time + offset`.

## Test vectors

| File | Value | Bytes |
|---|---|---|
| `hello_pc.bin` | Hello{1} | `01 02 00 00 00 01 00` |
| `hello_phone.bin` | Hello{1, "iPhone16,1", [1280×720@60, 1920×1080@60, 3840×2160@30]} | `01 1F 00 00 00 01 00 0A 00 69 50 68 6F 6E 65 31 36 2C 31 03 00 00 05 D0 02 3C 80 07 38 04 3C 00 0F 70 08 1E` |
| `configure_1080p60.bin` | Configure{1920, 1080, 60, 25000} | `02 09 00 00 00 80 07 38 04 3C A8 61 00 00` |
| `video_frame_key.bin` | VideoFrame{123456789, key, `00 00 00 01 67 42 00 1F`} | `03 11 00 00 00 15 CD 5B 07 00 00 00 00 01 00 00 00 01 67 42 00 1F` |
| `request_keyframe.bin` | RequestKeyframe | `04 00 00 00 00` |
| `ping.bin` | Ping{1000} | `05 08 00 00 00 E8 03 00 00 00 00 00 00` |
| `pong.bin` | Pong{1000, 2000} | `06 10 00 00 00 E8 03 00 00 00 00 00 00 D0 07 00 00 00 00 00 00` |
| `status_thermal.bin` | Status{thermal throttled, 2, "serious"} | `07 0B 00 00 00 02 02 07 00 73 65 72 69 6F 75 73` |
