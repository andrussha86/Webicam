#include "test_encoder.h"

#include <codecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <strmif.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>

#include <cstring>

#include "webicam/media_foundation.h"

using Microsoft::WRL::ComPtr;

namespace webicam {

namespace {

bool ContainsSps(const std::vector<uint8_t>& annexB) {
  for (size_t i = 0; i + 4 < annexB.size(); ++i) {
    if (annexB[i] == 0 && annexB[i + 1] == 0 && annexB[i + 2] == 1 && (annexB[i + 3] & 0x1F) == 7) return true;
  }
  return false;
}

void SetCodecValue(ICodecAPI* codec, const GUID& key, UINT32 value) {
  VARIANT v;
  VariantInit(&v);
  v.vt = VT_UI4;
  v.ulVal = value;
  codec->SetValue(&key, &v);
}

void SetCodecBool(ICodecAPI* codec, const GUID& key, bool value) {
  VARIANT v;
  VariantInit(&v);
  v.vt = VT_BOOL;
  v.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
  codec->SetValue(&key, &v);
}

}  // namespace

std::unique_ptr<TestPatternEncoder> TestPatternEncoder::Create(uint32_t width, uint32_t height, uint32_t fps,
                                                               uint32_t bitrateKbps) {
  EnsureMediaFoundation();
  std::unique_ptr<TestPatternEncoder> encoder(new TestPatternEncoder());
  encoder->width_ = width;
  encoder->height_ = height;
  encoder->fps_ = fps;
  if (FAILED(CoCreateInstance(CLSID_MSH264EncoderMFT, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&encoder->transform_)))) {
    return nullptr;
  }
  if (SUCCEEDED(encoder->transform_->QueryInterface(IID_PPV_ARGS(&encoder->codec_)))) {
    SetCodecBool(encoder->codec_, CODECAPI_AVLowLatencyMode, true);
    SetCodecValue(encoder->codec_, CODECAPI_AVEncMPVDefaultBPictureCount, 0);
    SetCodecValue(encoder->codec_, CODECAPI_AVEncMPVGOPSize, fps * 2);
    SetCodecValue(encoder->codec_, CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_CBR);
  }

  ComPtr<IMFMediaType> output;
  MFCreateMediaType(&output);
  output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
  output->SetUINT32(MF_MT_AVG_BITRATE, bitrateKbps * 1000);
  MFSetAttributeSize(output.Get(), MF_MT_FRAME_SIZE, width, height);
  MFSetAttributeRatio(output.Get(), MF_MT_FRAME_RATE, fps, 1);
  MFSetAttributeRatio(output.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
  output->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  output->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Main);
  if (FAILED(encoder->transform_->SetOutputType(0, output.Get(), 0))) return nullptr;

  ComPtr<IMFMediaType> input;
  MFCreateMediaType(&input);
  input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
  MFSetAttributeSize(input.Get(), MF_MT_FRAME_SIZE, width, height);
  MFSetAttributeRatio(input.Get(), MF_MT_FRAME_RATE, fps, 1);
  input->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  if (FAILED(encoder->transform_->SetInputType(0, input.Get(), 0))) return nullptr;

  ComPtr<IMFMediaType> current;
  if (SUCCEEDED(encoder->transform_->GetOutputCurrentType(0, &current))) {
    UINT32 size = 0;
    if (SUCCEEDED(current->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &size)) && size > 0) {
      encoder->sequenceHeader_.resize(size);
      current->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, encoder->sequenceHeader_.data(), size, nullptr);
    }
  }
  encoder->transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
  encoder->transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
  return encoder;
}

TestPatternEncoder::~TestPatternEncoder() {
  if (codec_) codec_->Release();
  if (transform_) transform_->Release();
}

std::vector<uint8_t> TestPatternEncoder::EncodeNext(bool forceKeyframe, uint64_t& frameIndex, bool& keyframe) {
  const uint64_t index = next_++;
  const LONGLONG period = 10'000'000LL / fps_;
  const DWORD bytes = width_ * height_ * 3 / 2;

  ComPtr<IMFMediaBuffer> buffer;
  MFCreateMemoryBuffer(bytes, &buffer);
  BYTE* data = nullptr;
  buffer->Lock(&data, nullptr, nullptr);
  std::memset(data, LumaFor(index), size_t(width_) * height_);
  std::memset(data + size_t(width_) * height_, 128, size_t(width_) * height_ / 2);
  buffer->Unlock();
  buffer->SetCurrentLength(bytes);
  ComPtr<IMFSample> sample;
  MFCreateSample(&sample);
  sample->AddBuffer(buffer.Get());
  sample->SetSampleTime(LONGLONG(index) * period);
  sample->SetSampleDuration(period);
  if (forceKeyframe && codec_) SetCodecValue(codec_, CODECAPI_AVEncVideoForceKeyFrame, 1);
  transform_->ProcessInput(0, sample.Get(), 0);

  std::vector<uint8_t> result;
  keyframe = false;
  while (true) {
    MFT_OUTPUT_STREAM_INFO info{};
    transform_->GetOutputStreamInfo(0, &info);
    ComPtr<IMFMediaBuffer> outBuffer;
    MFCreateMemoryBuffer(std::max<DWORD>(info.cbSize, bytes), &outBuffer);
    ComPtr<IMFSample> outSample;
    MFCreateSample(&outSample);
    outSample->AddBuffer(outBuffer.Get());
    MFT_OUTPUT_DATA_BUFFER output{};
    output.pSample = outSample.Get();
    DWORD status = 0;
    const HRESULT hr = transform_->ProcessOutput(0, 1, &output, &status);
    if (output.pEvents) output.pEvents->Release();
    if (FAILED(hr)) break;
    LONGLONG time = 0;
    outSample->GetSampleTime(&time);
    frameIndex = uint64_t((time + period / 2) / period);
    UINT32 clean = FALSE;
    if (SUCCEEDED(outSample->GetUINT32(MFSampleExtension_CleanPoint, &clean)) && clean) keyframe = true;
    BYTE* encoded = nullptr;
    DWORD length = 0;
    outBuffer->Lock(&encoded, nullptr, &length);
    result.assign(encoded, encoded + length);
    outBuffer->Unlock();
  }
  if (keyframe && !result.empty() && !ContainsSps(result) && !sequenceHeader_.empty()) {
    result.insert(result.begin(), sequenceHeader_.begin(), sequenceHeader_.end());
  }
  return result;
}

}  // namespace webicam
