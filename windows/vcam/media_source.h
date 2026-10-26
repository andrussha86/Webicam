#pragma once

#include "media_stream.h"

namespace webicam::vcam {

// Media source hosted by Windows Frame Server for the "Webicam" virtual camera.
class MediaSource
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          Microsoft::WRL::ChainInterfaces<IMFMediaSourceEx, IMFMediaSource, IMFMediaEventGenerator>, IMFGetService,
          IKsControl> {
 public:
  HRESULT RuntimeClassInitialize();
  bool IsShutdown() {
    std::lock_guard lock(mutex_);
    return !queue_;
  }

  // IMFMediaEventGenerator
  STDMETHODIMP BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) override;
  STDMETHODIMP EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) override;
  STDMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** event) override;
  STDMETHODIMP QueueEvent(MediaEventType type, REFGUID extended, HRESULT status, const PROPVARIANT* value) override;

  // IMFMediaSource
  STDMETHODIMP CreatePresentationDescriptor(IMFPresentationDescriptor** descriptor) override;
  STDMETHODIMP GetCharacteristics(DWORD* characteristics) override;
  STDMETHODIMP Pause() override;
  STDMETHODIMP Shutdown() override;
  STDMETHODIMP Start(IMFPresentationDescriptor* descriptor, const GUID* timeFormat,
                     const PROPVARIANT* startPosition) override;
  STDMETHODIMP Stop() override;

  // IMFMediaSourceEx
  STDMETHODIMP GetSourceAttributes(IMFAttributes** attributes) override;
  STDMETHODIMP GetStreamAttributes(DWORD streamId, IMFAttributes** attributes) override;
  STDMETHODIMP SetD3DManager(IUnknown* manager) override;

  // IMFGetService
  STDMETHODIMP GetService(REFGUID service, REFIID riid, LPVOID* object) override;

  // IKsControl
  STDMETHODIMP KsProperty(PKSPROPERTY, ULONG, LPVOID, ULONG, ULONG*) override;
  STDMETHODIMP KsMethod(PKSMETHOD, ULONG, LPVOID, ULONG, ULONG*) override;
  STDMETHODIMP KsEvent(PKSEVENT, ULONG, LPVOID, ULONG, ULONG*) override;

 private:
  std::mutex mutex_;
  Microsoft::WRL::ComPtr<IMFMediaEventQueue> queue_;
  Microsoft::WRL::ComPtr<IMFAttributes> attributes_;
  Microsoft::WRL::ComPtr<IMFPresentationDescriptor> presentation_;
  Microsoft::WRL::ComPtr<MediaStream> stream_;
  bool started_ = false;
};

}  // namespace webicam::vcam
