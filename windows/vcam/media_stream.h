#pragma once

#include <windows.h>
// Media Foundation headers must precede ks.h: ks.h redefines GUID_NULL as a macro.
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <ks.h>
#include <ksproxy.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <mutex>
#include <optional>
#include <vector>

#include "overlay.h"
#include "webicam/frame_ring.h"
#include "webicam/shared_section.h"
#include "webicam/vcam_logic.h"

namespace webicam::vcam {

class MediaSource;

// The single NV12 video stream. Pulls the newest frame from the shared ring
// whenever Frame Server asks for a sample.
class MediaStream
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          Microsoft::WRL::ChainInterfaces<IMFMediaStream2, IMFMediaStream, IMFMediaEventGenerator>, IKsControl> {
 public:
  HRESULT RuntimeClassInitialize(MediaSource* source);

  // IMFMediaEventGenerator
  STDMETHODIMP BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) override;
  STDMETHODIMP EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) override;
  STDMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** event) override;
  STDMETHODIMP QueueEvent(MediaEventType type, REFGUID extended, HRESULT status, const PROPVARIANT* value) override;

  // IMFMediaStream
  STDMETHODIMP GetMediaSource(IMFMediaSource** source) override;
  STDMETHODIMP GetStreamDescriptor(IMFStreamDescriptor** descriptor) override;
  STDMETHODIMP RequestSample(IUnknown* token) override;

  // IMFMediaStream2
  STDMETHODIMP SetStreamState(MF_STREAM_STATE state) override;
  STDMETHODIMP GetStreamState(MF_STREAM_STATE* state) override;

  // IKsControl
  STDMETHODIMP KsProperty(PKSPROPERTY, ULONG, LPVOID, ULONG, ULONG*) override;
  STDMETHODIMP KsMethod(PKSMETHOD, ULONG, LPVOID, ULONG, ULONG*) override;
  STDMETHODIMP KsEvent(PKSEVENT, ULONG, LPVOID, ULONG, ULONG*) override;

  HRESULT Start(IMFMediaType* type);
  HRESULT Stop();
  HRESULT Shutdown();
  IMFStreamDescriptor* descriptor() const { return descriptor_.Get(); }

 private:
  HRESULT FillFrame(uint8_t* nv12);
  void EnsureShared();

  std::mutex mutex_;
  Microsoft::WRL::ComPtr<IMFMediaEventQueue> queue_;
  Microsoft::WRL::ComPtr<IMFStreamDescriptor> descriptor_;
  Microsoft::WRL::ComPtr<IMFAttributes> attributes_;
  MediaSource* source_ = nullptr;  // owner; outlives the stream
  MF_STREAM_STATE state_ = MF_STREAM_STATE_STOPPED;
  OutputFormat format_{1920, 1080, 30};

  std::optional<SharedSection> section_;
  std::optional<FrameRing> ring_;
  HANDLE frameEvent_ = nullptr;
  uint64_t lastGeneration_ = 0;
  std::optional<FrameInfo> lastInfo_;
  std::vector<uint8_t> lastFrame_;
  LONGLONG lastDeliveryTime_ = 0;
  TextOverlay overlay_;
};

}  // namespace webicam::vcam
