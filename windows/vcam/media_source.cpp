#include "media_source.h"

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::MakeAndInitialize;

namespace webicam::vcam {

HRESULT MediaSource::RuntimeClassInitialize() {
  HRESULT hr = MFCreateEventQueue(&queue_);
  if (FAILED(hr)) return hr;
  hr = MFCreateAttributes(&attributes_, 1);
  if (FAILED(hr)) return hr;
  hr = MakeAndInitialize<MediaStream>(&stream_, this);
  if (FAILED(hr)) return hr;
  IMFStreamDescriptor* descriptors[] = {stream_->descriptor()};
  hr = MFCreatePresentationDescriptor(1, descriptors, &presentation_);
  if (FAILED(hr)) return hr;
  return presentation_->SelectStream(0);
}

STDMETHODIMP MediaSource::BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) {
  std::lock_guard lock(mutex_);
  return queue_ ? queue_->BeginGetEvent(callback, state) : MF_E_SHUTDOWN;
}

STDMETHODIMP MediaSource::EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) {
  std::lock_guard lock(mutex_);
  return queue_ ? queue_->EndGetEvent(result, event) : MF_E_SHUTDOWN;
}

STDMETHODIMP MediaSource::GetEvent(DWORD flags, IMFMediaEvent** event) {
  ComPtr<IMFMediaEventQueue> queue;
  {
    std::lock_guard lock(mutex_);
    queue = queue_;
  }
  return queue ? queue->GetEvent(flags, event) : MF_E_SHUTDOWN;
}

STDMETHODIMP MediaSource::QueueEvent(MediaEventType type, REFGUID extended, HRESULT status, const PROPVARIANT* value) {
  std::lock_guard lock(mutex_);
  return queue_ ? queue_->QueueEventParamVar(type, extended, status, value) : MF_E_SHUTDOWN;
}

STDMETHODIMP MediaSource::CreatePresentationDescriptor(IMFPresentationDescriptor** descriptor) {
  if (!descriptor) return E_POINTER;
  std::lock_guard lock(mutex_);
  if (!queue_) return MF_E_SHUTDOWN;
  return presentation_->Clone(descriptor);
}

STDMETHODIMP MediaSource::GetCharacteristics(DWORD* characteristics) {
  if (!characteristics) return E_POINTER;
  *characteristics = MFMEDIASOURCE_IS_LIVE;
  return S_OK;
}

STDMETHODIMP MediaSource::Start(IMFPresentationDescriptor* descriptor, const GUID* timeFormat,
                                const PROPVARIANT* startPosition) {
  if (!descriptor || !startPosition) return E_INVALIDARG;
  if (timeFormat && *timeFormat != GUID_NULL) return MF_E_UNSUPPORTED_TIME_FORMAT;
  std::lock_guard lock(mutex_);
  if (!queue_) return MF_E_SHUTDOWN;

  BOOL selected = FALSE;
  ComPtr<IMFStreamDescriptor> streamDescriptor;
  HRESULT hr = descriptor->GetStreamDescriptorByIndex(0, &selected, &streamDescriptor);
  if (FAILED(hr)) return hr;
  ComPtr<IMFMediaTypeHandler> handler;
  ComPtr<IMFMediaType> type;
  if (SUCCEEDED(streamDescriptor->GetMediaTypeHandler(&handler))) handler->GetCurrentMediaType(&type);

  PROPVARIANT streamValue;
  PropVariantInit(&streamValue);
  streamValue.vt = VT_UNKNOWN;
  streamValue.punkVal = static_cast<IMFMediaStream*>(stream_.Get());
  hr = queue_->QueueEventParamVar(started_ ? MEUpdatedStream : MENewStream, GUID_NULL, S_OK, &streamValue);
  if (FAILED(hr)) return hr;

  hr = stream_->Start(type.Get());
  if (FAILED(hr)) return hr;
  started_ = true;

  PROPVARIANT time;
  PropVariantInit(&time);
  time.vt = VT_I8;
  time.hVal.QuadPart = MFGetSystemTime();
  return queue_->QueueEventParamVar(MESourceStarted, GUID_NULL, S_OK, &time);
}

STDMETHODIMP MediaSource::Stop() {
  std::lock_guard lock(mutex_);
  if (!queue_) return MF_E_SHUTDOWN;
  HRESULT hr = stream_->Stop();
  if (FAILED(hr)) return hr;
  return queue_->QueueEventParamVar(MESourceStopped, GUID_NULL, S_OK, nullptr);
}

STDMETHODIMP MediaSource::Pause() { return MF_E_INVALID_STATE_TRANSITION; }

STDMETHODIMP MediaSource::Shutdown() {
  std::lock_guard lock(mutex_);
  if (!queue_) return MF_E_SHUTDOWN;
  if (stream_) stream_->Shutdown();
  queue_->Shutdown();
  queue_.Reset();
  return S_OK;
}

STDMETHODIMP MediaSource::GetSourceAttributes(IMFAttributes** attributes) {
  if (!attributes) return E_POINTER;
  std::lock_guard lock(mutex_);
  if (!queue_) return MF_E_SHUTDOWN;
  return attributes_.CopyTo(attributes);
}

STDMETHODIMP MediaSource::GetStreamAttributes(DWORD streamId, IMFAttributes** attributes) {
  if (!attributes) return E_POINTER;
  if (streamId != 0) return E_INVALIDARG;
  std::lock_guard lock(mutex_);
  if (!queue_) return MF_E_SHUTDOWN;
  return stream_->descriptor()->QueryInterface(IID_PPV_ARGS(attributes));
}

STDMETHODIMP MediaSource::SetD3DManager(IUnknown*) { return S_OK; }

STDMETHODIMP MediaSource::GetService(REFGUID, REFIID, LPVOID* object) {
  if (object) *object = nullptr;
  return MF_E_UNSUPPORTED_SERVICE;
}

STDMETHODIMP MediaSource::KsProperty(PKSPROPERTY, ULONG, LPVOID, ULONG, ULONG*) {
  return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}
STDMETHODIMP MediaSource::KsMethod(PKSMETHOD, ULONG, LPVOID, ULONG, ULONG*) {
  return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}
STDMETHODIMP MediaSource::KsEvent(PKSEVENT, ULONG, LPVOID, ULONG, ULONG*) {
  return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

}  // namespace webicam::vcam
