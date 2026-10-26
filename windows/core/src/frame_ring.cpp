#include "webicam/frame_ring.h"

#include <cstring>
#include <new>

namespace webicam {

using namespace shm;

namespace {
constexpr uint64_t PackLatest(uint64_t generation, uint32_t slot) { return (generation << 8) | slot; }
constexpr uint32_t LatestSlot(uint64_t latest) { return uint32_t(latest & 0xFF); }
constexpr uint64_t LatestGeneration(uint64_t latest) { return latest >> 8; }
}  // namespace

size_t FrameRing::RequiredBytes() { return kTotalBytes; }

FrameRing FrameRing::Initialize(void* mem) {
  auto* base = static_cast<uint8_t*>(mem);
  std::memset(base, 0, sizeof(ControlBlock));
  auto* control = new (base) ControlBlock{};
  control->readingSlot.store(kNoSlot);
  for (uint32_t i = 0; i < kSlotCount; ++i) {
    new (base + sizeof(ControlBlock) + kSlotStride * i) SlotHeader{};
  }
  std::atomic_thread_fence(std::memory_order_release);
  control->layoutVersion = kLayoutVersion;
  return FrameRing(base);
}

std::optional<FrameRing> FrameRing::Attach(void* mem) {
  auto* base = static_cast<uint8_t*>(mem);
  if (reinterpret_cast<ControlBlock*>(base)->layoutVersion != kLayoutVersion) return std::nullopt;
  FrameRing ring(base);
  ring.nextGeneration_ = LatestGeneration(ring.control().latest.load()) + 1;
  return ring;
}

ControlBlock& FrameRing::control() const { return *reinterpret_cast<ControlBlock*>(base_); }

SlotHeader& FrameRing::slot(uint32_t index) const {
  return *reinterpret_cast<SlotHeader*>(base_ + sizeof(ControlBlock) + kSlotStride * index);
}

uint8_t* FrameRing::slotData(uint32_t index) const {
  return base_ + sizeof(ControlBlock) + kSlotStride * index + sizeof(SlotHeader);
}

bool FrameRing::Publish(const FrameInfo& info, std::span<const uint8_t> nv12) {
  if (info.width == 0 || info.height == 0 || info.width > kMaxWidth || info.height > kMaxHeight) return false;
  if (nv12.size() > kSlotDataBytes) return false;

  auto& ctl = control();
  const uint64_t latest = ctl.latest.load(std::memory_order_acquire);
  const uint32_t published = latest == 0 ? kNoSlot : LatestSlot(latest);
  const uint32_t reading = ctl.readingSlot.load(std::memory_order_acquire);
  uint32_t target = 0;
  while (target == published || target == reading) ++target;

  auto& header = slot(target);
  header.generation.store(0, std::memory_order_release);
  std::atomic_thread_fence(std::memory_order_seq_cst);
  std::memcpy(slotData(target), nv12.data(), nv12.size());
  header.width = info.width;
  header.height = info.height;
  header.stride = info.stride;
  header.timestamp100ns = info.timestamp100ns;
  const uint64_t generation = nextGeneration_++;
  header.generation.store(generation, std::memory_order_release);
  ctl.latest.store(PackLatest(generation, target), std::memory_order_release);
  return true;
}

void FrameRing::SetHeartbeat(uint64_t nowMs, bool connected) {
  control().connected.store(connected ? 1 : 0, std::memory_order_release);
  control().heartbeatMs.store(nowMs, std::memory_order_release);
}

std::optional<std::array<uint32_t, 3>> FrameRing::RequestedFormat() const {
  const auto& ctl = control();
  std::array<uint32_t, 3> format{ctl.requestedWidth.load(), ctl.requestedHeight.load(), ctl.requestedFps.load()};
  if (format[0] == 0 || format[1] == 0 || format[2] == 0) return std::nullopt;
  return format;
}

std::optional<uint64_t> FrameRing::ReadLatest(uint64_t lastGeneration, FrameInfo& info,
                                              std::vector<uint8_t>& out) {
  auto& ctl = control();
  for (int attempt = 0; attempt < 3; ++attempt) {
    const uint64_t latest = ctl.latest.load(std::memory_order_acquire);
    if (latest == 0 || LatestGeneration(latest) <= lastGeneration) return std::nullopt;
    const uint32_t index = LatestSlot(latest);
    const uint64_t generation = LatestGeneration(latest);
    if (index >= kSlotCount) return std::nullopt;

    ctl.readingSlot.store(index, std::memory_order_seq_cst);
    if (ctl.latest.load(std::memory_order_seq_cst) != latest) continue;

    auto& header = slot(index);
    FrameInfo copy{header.width, header.height, header.stride, header.timestamp100ns};
    const size_t bytes = size_t(copy.stride) * copy.height * 3 / 2;
    if (copy.width > kMaxWidth || copy.height > kMaxHeight || copy.stride < copy.width || bytes > kSlotDataBytes) {
      ctl.readingSlot.store(kNoSlot);
      return std::nullopt;
    }
    out.resize(bytes);
    std::memcpy(out.data(), slotData(index), bytes);
    std::atomic_thread_fence(std::memory_order_acquire);
    const bool intact = header.generation.load(std::memory_order_acquire) == generation;
    ctl.readingSlot.store(kNoSlot, std::memory_order_release);
    if (!intact) continue;
    info = copy;
    return generation;
  }
  return std::nullopt;
}

void FrameRing::SetRequestedFormat(uint32_t width, uint32_t height, uint32_t fps) {
  auto& ctl = control();
  ctl.requestedWidth.store(width);
  ctl.requestedHeight.store(height);
  ctl.requestedFps.store(fps);
}

uint64_t FrameRing::HeartbeatMs() const { return control().heartbeatMs.load(std::memory_order_acquire); }

bool FrameRing::Connected() const { return control().connected.load(std::memory_order_acquire) != 0; }

}  // namespace webicam
