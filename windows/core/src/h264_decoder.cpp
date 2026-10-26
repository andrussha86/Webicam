#include "webicam/h264_decoder.h"

#include <codecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wmcodecdsp.h>

#include <cstring>

using Microsoft::WRL::ComPtr;

namespace webicam {

namespace {

bool LooksLikeAnnexB(std::span<const uint8_t> data) {
  if (data.size() >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1) return true;
  return data.size() >= 3 && data[0] == 0 && data[1] == 0 && data[2] == 1;
}

}  // namespace

std::unique_ptr<H264Decoder> H264Decoder::Create(const D3DContext& d3d) {
  EnsureMediaFoundation();
  std::unique_ptr<H264Decoder> decoder(new H264Decoder());
  if (FAILED(CoCreateInstance(CLSID_MSH264DecoderMFT, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&decoder->transform_)))) {
    return nullptr;
  }
  ComPtr<IMFAttributes> attributes;
  if (SUCCEEDED(decoder->transform_->GetAttributes(&attributes))) {
    attributes->SetUINT32(CODECAPI_AVLowLatencyMode, TRUE);
    UINT32 aware = FALSE;
    if (SUCCEEDED(attributes->GetUINT32(MF_SA_D3D11_AWARE, &aware)) && aware) {
      UINT token = 0;
      if (SUCCEEDED(MFCreateDXGIDeviceManager(&token, &decoder->manager_)) &&
          SUCCEEDED(decoder->manager_->ResetDevice(d3d.device.Get(), token))) {
        if (FAILED(decoder->transform_->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,
                                                       ULONG_PTR(decoder->manager_.Get())))) {
          decoder->manager_.Reset();
        }
      }
    }
  }
  if (!decoder->manager_) return nullptr;  // Frames must come out as D3D11 textures.

  ComPtr<IMFMediaType> input;
  MFCreateMediaType(&input);
  input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
  input->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  if (FAILED(decoder->transform_->SetInputType(0, input.Get(), 0))) return nullptr;
  if (!decoder->ConfigureOutput()) return nullptr;
  decoder->transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
  decoder->transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
  return decoder;
}

H264Decoder::~H264Decoder() {
  if (transform_) {
    transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
    transform_->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, 0);
  }
}

bool H264Decoder::ConfigureOutput() {
  for (DWORD i = 0;; ++i) {
    ComPtr<IMFMediaType> type;
    if (FAILED(transform_->GetOutputAvailableType(0, i, &type))) return false;
    GUID subtype{};
    type->GetGUID(MF_MT_SUBTYPE, &subtype);
    if (subtype != MFVideoFormat_NV12) continue;
    if (FAILED(transform_->SetOutputType(0, type.Get(), 0))) return false;
    UINT32 width = 0, height = 0;
    MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height);
    MFVideoArea area{};
    if (SUCCEEDED(type->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&area), sizeof(area),
                                nullptr))) {
      width = UINT32(area.Area.cx);
      height = UINT32(area.Area.cy);
    }
    width_ = width;
    height_ = height;
    return true;
  }
}

H264Decoder::Result H264Decoder::Decode(std::span<const uint8_t> annexB, int64_t timestamp100ns, DecodedFrame& out) {
  if (!LooksLikeAnnexB(annexB)) return Result::Error;

  ComPtr<IMFMediaBuffer> buffer;
  if (FAILED(MFCreateMemoryBuffer(DWORD(annexB.size()), &buffer))) return Result::Error;
  BYTE* data = nullptr;
  buffer->Lock(&data, nullptr, nullptr);
  std::memcpy(data, annexB.data(), annexB.size());
  buffer->Unlock();
  buffer->SetCurrentLength(DWORD(annexB.size()));
  ComPtr<IMFSample> sample;
  MFCreateSample(&sample);
  sample->AddBuffer(buffer.Get());
  sample->SetSampleTime(timestamp100ns);

  bool produced = false;
  auto drain = [&]() -> bool {
    while (true) {
      MFT_OUTPUT_DATA_BUFFER output{};
      DWORD status = 0;
      const HRESULT hr = transform_->ProcessOutput(0, 1, &output, &status);
      if (output.pEvents) output.pEvents->Release();
      if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) return true;
      if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
        if (output.pSample) output.pSample->Release();
        if (!ConfigureOutput()) return false;
        continue;
      }
      if (FAILED(hr)) {
        if (output.pSample) output.pSample->Release();
        return false;
      }
      ComPtr<IMFSample> decoded;
      decoded.Attach(output.pSample);
      ComPtr<IMFMediaBuffer> media;
      ComPtr<IMFDXGIBuffer> dxgi;
      if (!decoded || FAILED(decoded->GetBufferByIndex(0, &media)) || FAILED(media.As(&dxgi))) return false;
      DecodedFrame frame;
      if (FAILED(dxgi->GetResource(IID_PPV_ARGS(&frame.texture)))) return false;
      dxgi->GetSubresourceIndex(&frame.subresource);
      LONGLONG time = 0;
      decoded->GetSampleTime(&time);
      frame.timestamp100ns = time;
      frame.width = width_;
      frame.height = height_;
      out = std::move(frame);
      produced = true;
    }
  };

  HRESULT hr = transform_->ProcessInput(0, sample.Get(), 0);
  if (hr == MF_E_NOTACCEPTING) {
    if (!drain()) return Result::Error;
    hr = transform_->ProcessInput(0, sample.Get(), 0);
  }
  if (FAILED(hr)) return Result::Error;
  if (!drain()) return Result::Error;
  return produced ? Result::Frame : Result::NeedMore;
}

void H264Decoder::Reset() {
  transform_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
  transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
}

}  // namespace webicam
