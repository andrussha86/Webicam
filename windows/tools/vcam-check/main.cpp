// Opens the "Webicam" camera like any consumer app and reports what it sees.
//   vcam-check [--frames N] [--width W --height H]
// Prints the first luma values of the frames so the probe's color bars
// (Y = 235 at the left edge when not scrolled) can be told from the placeholder (Y ≈ 32).
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdio>
#include <string>

using Microsoft::WRL::ComPtr;

int wmain(int argc, wchar_t** argv) {
  int frames = 30;
  UINT32 width = 1280, height = 720;
  for (int i = 1; i + 1 < argc; ++i) {
    std::wstring arg = argv[i];
    if (arg == L"--frames") frames = _wtoi(argv[i + 1]);
    if (arg == L"--width") width = UINT32(_wtoi(argv[i + 1]));
    if (arg == L"--height") height = UINT32(_wtoi(argv[i + 1]));
  }
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  MFStartup(MF_VERSION);

  ComPtr<IMFAttributes> query;
  MFCreateAttributes(&query, 1);
  query->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  IMFActivate** devices = nullptr;
  UINT32 count = 0;
  if (FAILED(MFEnumDeviceSources(query.Get(), &devices, &count))) return 1;
  ComPtr<IMFMediaSource> source;
  for (UINT32 i = 0; i < count; ++i) {
    wchar_t* name = nullptr;
    UINT32 length = 0;
    devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &length);
    std::wprintf(L"camera: %s\n", name ? name : L"?");
    if (name && std::wstring(name).starts_with(L"Webicam") && !source) devices[i]->ActivateObject(IID_PPV_ARGS(&source));
    CoTaskMemFree(name);
    devices[i]->Release();
  }
  CoTaskMemFree(devices);
  if (!source) {
    std::printf("FAIL: camera 'Webicam' not found\n");
    return 2;
  }

  ComPtr<IMFSourceReader> reader;
  HRESULT hr = MFCreateSourceReaderFromMediaSource(source.Get(), nullptr, &reader);
  if (FAILED(hr)) {
    std::printf("FAIL: source reader 0x%08X\n", unsigned(hr));
    return 3;
  }
  ComPtr<IMFMediaType> type;
  MFCreateMediaType(&type);
  type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
  MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, width, height);
  hr = reader->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, type.Get());
  std::printf("set %ux%u NV12: 0x%08X\n", width, height, unsigned(hr));

  const auto start = std::chrono::steady_clock::now();
  for (int n = 0; n < frames; ++n) {
    DWORD stream = 0, flags = 0;
    LONGLONG time = 0;
    ComPtr<IMFSample> sample;
    hr = reader->ReadSample(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &stream, &flags, &time, &sample);
    if (FAILED(hr)) {
      std::printf("FAIL: ReadSample 0x%08X\n", unsigned(hr));
      return 4;
    }
    if (!sample) continue;
    ComPtr<IMFMediaBuffer> buffer;
    sample->ConvertToContiguousBuffer(&buffer);
    BYTE* data = nullptr;
    DWORD size = 0;
    buffer->Lock(&data, nullptr, &size);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    std::printf("frame %2d t=%5lldms size=%lu Y[0]=%u Y[mid]=%u\n", n, ms.count(), size, data[0],
                data[size_t(width) * (height / 2) + width / 2]);
    buffer->Unlock();
  }
  return 0;
}
