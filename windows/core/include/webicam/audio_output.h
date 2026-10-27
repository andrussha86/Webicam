#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace webicam {

// Keeps the phone's samples between the network and the sound card. It waits until `target` samples
// are buffered before playing, plays silence when it runs dry (and waits for `target` again), and drops
// the oldest samples when more than `limit` pile up, so latency never grows. Thread-safe.
class AudioJitterBuffer {
 public:
  AudioJitterBuffer(size_t target, size_t limit) : target_(target), limit_(limit) {}

  void Push(std::span<const int16_t> samples);
  // Always fills `out`, with silence where nothing is available.
  void Pull(std::span<int16_t> out);
  void Reset();
  size_t Buffered() const;
  // Peak of the recent input, 0…1, decaying; for a level meter.
  float Level() const;

 private:
  mutable std::mutex mutex_;
  std::deque<int16_t> samples_;
  size_t target_;
  size_t limit_;
  bool playing_ = false;
  float level_ = 0;
};

struct AudioDevice {
  std::string id;    // endpoint id, stable across runs
  std::string name;  // friendly name, e.g. "CABLE Input (VB-Audio Virtual Cable)"
  bool operator==(const AudioDevice&) const = default;
};

// Active playback devices.
std::vector<AudioDevice> ListAudioOutputs();
// The VB-Audio Virtual Cable input, the usual way to give other apps a microphone; "" when not installed.
std::string FindVirtualCable(const std::vector<AudioDevice>& devices);

// Plays 48 kHz mono samples on one playback device (WASAPI, shared mode) from its own thread, reopening
// the device when it disappears and comes back.
class AudioOutput {
 public:
  AudioOutput();
  AudioOutput(const AudioOutput&) = delete;
  AudioOutput& operator=(const AudioOutput&) = delete;
  ~AudioOutput();

  // "" stops playing. Safe to call every frame with the same id.
  void SetDevice(const std::string& deviceId);
  std::string Device() const;
  void Push(std::span<const int16_t> samples) { buffer_.Push(samples); }
  void Reset() { buffer_.Reset(); }
  float Level() const { return buffer_.Level(); }
  // Russian reason the device is not playing, "" while fine.
  std::string Error() const;

  static constexpr size_t kTargetSamples = 48000 * 40 / 1000;  // 40 ms before playing starts
  static constexpr size_t kLimitSamples = 48000 * 150 / 1000;  // more than 150 ms behind is dropped

 private:
  void Run();
  void SetError(std::string error);

  AudioJitterBuffer buffer_{kTargetSamples, kLimitSamples};
  mutable std::mutex mutex_;
  std::string device_;
  std::string error_;
  std::atomic<uint32_t> generation_{0};  // bumped on every device change
  std::atomic<bool> stop_{false};
  std::thread thread_;
};

}  // namespace webicam
