#include "media_stream.h"

#include <sddl.h>

#include <cstring>

#include "media_source.h"

using Microsoft::WRL::ComPtr;

namespace webicam::vcam {

namespace {

constexpr wchar_t kPlaceholderText[] = L"Webicam: телефон не подключён";
constexpr wchar_t kNoSignalText[] = L"Нет сигнала";

HRESULT CreateNv12Type(const OutputFormat& format, IMFMediaType** out) {
  ComPtr<IMFMediaType> type;
  HRESULT hr = MFCreateMediaType(&type);
  if (FAILED(hr)) return hr;
  type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
  MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, format.width, format.height);
  MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, format.fps, 1);
  MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
  type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
  type->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE);
  type->SetUINT32(MF_MT_DEFAULT_STRIDE, format.width);
  type->SetUINT32(MF_MT_SAMPLE_SIZE, format.width * format.height * 3 / 2);
  *out = type.Detach();
  return S_OK;
}

}  // namespace

HRESULT MediaStream::RuntimeClassInitialize(MediaSource* source) {
  source_ = source;
  HRESULT hr = MFCreateEventQueue(&queue_);
  if (FAILED(hr)) return hr;

  const auto formats = AdvertisedFormats();
  std::vector<ComPtr<IMFMediaType>> types(formats.size());
  std::vector<IMFMediaType*> raw(formats.size());
  for (size_t i = 0; i < formats.size(); ++i) {
    hr = CreateNv12Type(formats[i], &types[i]);
    if (FAILED(hr)) return hr;
    raw[i] = types[i].Get();
  }
  hr = MFCreateStreamDescriptor(0, DWORD(raw.size()), raw.data(), &descriptor_);
  if (FAILED(hr)) return hr;
  ComPtr<IMFMediaTypeHandler> handler;
  descriptor_->GetMediaTypeHandler(&handler);
  handler->SetCurrentMediaType(raw[0]);

  descriptor_.As(&attributes_);
  attributes_->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, PINNAME_VIDEO_CAPTURE);
  attributes_->SetUINT32(MF_DEVICESTREAM_STREAM_ID, 0);
  attributes_->SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1);
  attributes_->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES, MFFrameSourceTypes_Color);
  return S_OK;
}

STDMETHODIMP MediaStream::BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) {
  std::lock_guard lock(mutex_);
  return queue_ ? queue_->BeginGetEvent(callback, state) : MF_E_SHUTDOWN;
}

STDMETHODIMP MediaStream::EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) {
  std::lock_guard lock(mutex_);
  return queue_ ? queue_->EndGetEvent(result, event) : MF_E_SHUTDOWN;
}

STDMETHODIMP MediaStream::GetEvent(DWORD flags, IMFMediaEvent** event) {
  ComPtr<IMFMediaEventQueue> queue;
  {
    std::lock_guard lock(mutex_);
    queue = queue_;
  }
  return queue ? queue->GetEvent(flags, event) : MF_E_SHUTDOWN;
}

STDMETHODIMP MediaStream::QueueEvent(MediaEventType type, REFGUID extended, HRESULT status, const PROPVARIANT* value) {
  std::lock_guard lock(mutex_);
  return queue_ ? queue_->QueueEventParamVar(type, extended, status, value) : MF_E_SHUTDOWN;
}

STDMETHODIMP MediaStream::GetMediaSource(IMFMediaSource** source) {
  if (!source) return E_POINTER;
  std::lock_guard lock(mutex_);
  if (!queue_) return MF_E_SHUTDOWN;
  return source_->QueryInterface(IID_PPV_ARGS(source));
}

STDMETHODIMP MediaStream::GetStreamDescriptor(IMFStreamDescriptor** descriptor) {
  if (!descriptor) return E_POINTER;
  std::lock_guard lock(mutex_);
  if (!queue_) return MF_E_SHUTDOWN;
  return descriptor_.CopyTo(descriptor);
}

void MediaStream::EnsureShared() {
  if (ring_) return;
  auto section = SharedSection::Create(shm::kSectionName, FrameRing::RequiredBytes(), shm::kSectionSddl);
  if (!section) return;
  ring_ = section->existed() ? FrameRing::Attach(section->data()) : FrameRing::Initialize(section->data());
  if (!ring_) ring_ = FrameRing::Initialize(section->data());
  section_ = std::move(section);
  if (!frameEvent_) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(shm::kSectionSddl, SDDL_REVISION_1, &descriptor, nullptr)) {
      SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
      frameEvent_ = CreateEventW(&attributes, FALSE, FALSE, shm::kFrameEventName);
      LocalFree(descriptor);
    }
  }
}

HRESULT MediaStream::Start(IMFMediaType* type) {
  std::lock_guard lock(mutex_);
  if (!queue_) return MF_E_SHUTDOWN;
  if (type) {
    UINT32 width = 0, height = 0, numerator = 0, denominator = 1;
    MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height);
    MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &numerator, &denominator);
    format_ = {width, height, denominator ? numerator / denominator : 30};
  }
  EnsureShared();
  if (ring_) ring_->SetRequestedFormat(format_.width, format_.height, format_.fps);
  state_ = MF_STREAM_STATE_RUNNING;
  lastDeliveryTime_ = 0;
  return queue_->QueueEventParamVar(MEStreamStarted, GUID_NULL, S_OK, nullptr);
}

HRESULT MediaStream::Stop() {
  std::lock_guard lock(mutex_);
  if (!queue_) return MF_E_SHUTDOWN;
  state_ = MF_STREAM_STATE_STOPPED;
  if (ring_) ring_->SetRequestedFormat(0, 0, 0);
  return queue_->QueueEventParamVar(MEStreamStopped, GUID_NULL, S_OK, nullptr);
}

HRESULT MediaStream::Shutdown() {
  std::lock_guard lock(mutex_);
  if (queue_) queue_->Shutdown();
  queue_.Reset();
  if (ring_) ring_->SetRequestedFormat(0, 0, 0);
  ring_.reset();
  section_.reset();
  if (frameEvent_) CloseHandle(frameEvent_);
  frameEvent_ = nullptr;
  return S_OK;
}

HRESULT MediaStream::FillFrame(uint8_t* nv12) {
  const uint32_t width = format_.width;
  const uint32_t height = format_.height;
  if (ring_) {
    FrameInfo info{};
    std::vector<uint8_t> scratch;
    if (auto generation = ring_->ReadLatest(lastGeneration_, info, scratch)) {
      lastGeneration_ = *generation;
      lastInfo_ = info;
      lastFrame_ = std::move(scratch);
    }
  }

  const uint64_t heartbeat = ring_ ? ring_->HeartbeatMs() : 0;
  const bool connected = ring_ && ring_->Connected();
  const OutputKind kind = ChooseOutput(SteadyNowMs(), heartbeat, connected, lastInfo_, width, height);
  if (kind == OutputKind::Placeholder) {
    FillPlaceholder(nv12, width, height);
    overlay_.Draw(nv12, width, height, kPlaceholderText);
    return S_OK;
  }
  // Copy row by row: the ring's stride may exceed the output width.
  for (uint32_t row = 0; row < height * 3 / 2; ++row) {
    std::memcpy(nv12 + size_t(row) * width, lastFrame_.data() + size_t(row) * lastInfo_->stride, width);
  }
  if (kind == OutputKind::NoSignalDimmed) {
    Dim(nv12, width, height);
    overlay_.Draw(nv12, width, height, kNoSignalText);
  }
  return S_OK;
}

STDMETHODIMP MediaStream::RequestSample(IUnknown* token) {
  // Wait (outside the lock) for the next published frame, at most one frame period.
  HANDLE event = nullptr;
  uint32_t fps = 30;
  {
    std::lock_guard lock(mutex_);
    if (!queue_) return MF_E_SHUTDOWN;
    if (state_ != MF_STREAM_STATE_RUNNING) return MF_E_INVALIDREQUEST;
    event = frameEvent_;
    fps = format_.fps ? format_.fps : 30;
  }
  const DWORD periodMs = DWORD(1000 / fps);
  if (event) {
    WaitForSingleObject(event, periodMs);
  } else {
    Sleep(periodMs);
  }

  std::lock_guard lock(mutex_);
  if (!queue_) return MF_E_SHUTDOWN;
  const DWORD bytes = format_.width * format_.height * 3 / 2;
  ComPtr<IMFMediaBuffer> buffer;
  HRESULT hr = MFCreateMemoryBuffer(bytes, &buffer);
  if (FAILED(hr)) return hr;
  BYTE* data = nullptr;
  hr = buffer->Lock(&data, nullptr, nullptr);
  if (FAILED(hr)) return hr;
  FillFrame(data);
  buffer->Unlock();
  buffer->SetCurrentLength(bytes);

  ComPtr<IMFSample> sample;
  hr = MFCreateSample(&sample);
  if (FAILED(hr)) return hr;
  sample->AddBuffer(buffer.Get());
  const LONGLONG now = MFGetSystemTime();
  sample->SetSampleTime(now);
  sample->SetSampleDuration(10'000'000 / fps);
  if (lastDeliveryTime_ == 0) sample->SetUINT32(MFSampleExtension_Discontinuity, TRUE);
  lastDeliveryTime_ = now;
  if (token) sample->SetUnknown(MFSampleExtension_Token, token);
  return queue_->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, sample.Get());
}

STDMETHODIMP MediaStream::SetStreamState(MF_STREAM_STATE state) {
  std::lock_guard lock(mutex_);
  if (!queue_) return MF_E_SHUTDOWN;
  state_ = state;
  return S_OK;
}

STDMETHODIMP MediaStream::GetStreamState(MF_STREAM_STATE* state) {
  if (!state) return E_POINTER;
  std::lock_guard lock(mutex_);
  if (!queue_) return MF_E_SHUTDOWN;
  *state = state_;
  return S_OK;
}

STDMETHODIMP MediaStream::KsProperty(PKSPROPERTY, ULONG, LPVOID, ULONG, ULONG*) {
  return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}
STDMETHODIMP MediaStream::KsMethod(PKSMETHOD, ULONG, LPVOID, ULONG, ULONG*) {
  return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}
STDMETHODIMP MediaStream::KsEvent(PKSEVENT, ULONG, LPVOID, ULONG, ULONG*) {
  return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

}  // namespace webicam::vcam
