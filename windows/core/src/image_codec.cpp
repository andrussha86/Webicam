#include "webicam/image_codec.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>

namespace webicam {

namespace {

using Microsoft::WRL::ComPtr;

ComPtr<IWICImagingFactory> Factory() {
  ComPtr<IWICImagingFactory> factory;
  CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
  return factory;
}

std::optional<Bgra> CopyBgra(IWICImagingFactory* factory, IWICBitmapSource* source) {
  ComPtr<IWICBitmapSource> bgra;
  if (FAILED(WICConvertBitmapSource(GUID_WICPixelFormat32bppBGRA, source, &bgra))) return std::nullopt;
  UINT w = 0, h = 0;
  if (FAILED(bgra->GetSize(&w, &h)) || w == 0 || h == 0) return std::nullopt;
  Bgra image{w, h, std::vector<uint8_t>(size_t(w) * h * 4)};
  if (FAILED(bgra->CopyPixels(nullptr, w * 4, UINT(image.pixels.size()), image.pixels.data()))) return std::nullopt;
  (void)factory;
  return image;
}

std::optional<Bgra> FirstFrame(IWICImagingFactory* factory, IWICBitmapDecoder* decoder) {
  ComPtr<IWICBitmapFrameDecode> frame;
  if (FAILED(decoder->GetFrame(0, &frame))) return std::nullopt;
  return CopyBgra(factory, frame.Get());
}

ComPtr<IWICBitmap> Wrap(IWICImagingFactory* factory, const Bgra& image) {
  ComPtr<IWICBitmap> bitmap;
  factory->CreateBitmapFromMemory(image.width, image.height, GUID_WICPixelFormat32bppBGRA, image.width * 4,
                                  UINT(image.pixels.size()), const_cast<BYTE*>(image.pixels.data()), &bitmap);
  return bitmap;
}

}  // namespace

std::optional<Bgra> LoadImageFile(const std::filesystem::path& path) {
  auto factory = Factory();
  ComPtr<IWICBitmapDecoder> decoder;
  if (!factory || FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                           WICDecodeMetadataCacheOnDemand, &decoder))) {
    return std::nullopt;
  }
  return FirstFrame(factory.Get(), decoder.Get());
}

std::optional<Bgra> DecodeImage(std::span<const uint8_t> bytes) {
  auto factory = Factory();
  ComPtr<IWICStream> stream;
  ComPtr<IWICBitmapDecoder> decoder;
  if (!factory || FAILED(factory->CreateStream(&stream)) ||
      FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), DWORD(bytes.size()))) ||
      FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder))) {
    return std::nullopt;
  }
  return FirstFrame(factory.Get(), decoder.Get());
}

Bgra CoverResize(const Bgra& source, uint32_t width, uint32_t height) {
  auto factory = Factory();
  if (!factory || source.pixels.empty() || width == 0 || height == 0) return {};
  // Largest centred rectangle of the target aspect.
  WICRect crop{0, 0, INT(source.width), INT(source.height)};
  if (uint64_t(source.width) * height > uint64_t(source.height) * width) {
    crop.Width = std::max(1, INT(uint64_t(source.height) * width / height));
    crop.X = (INT(source.width) - crop.Width) / 2;
  } else {
    crop.Height = std::max(1, INT(uint64_t(source.width) * height / width));
    crop.Y = (INT(source.height) - crop.Height) / 2;
  }
  auto bitmap = Wrap(factory.Get(), source);
  ComPtr<IWICBitmapClipper> clipper;
  ComPtr<IWICBitmapScaler> scaler;
  if (!bitmap || FAILED(factory->CreateBitmapClipper(&clipper)) || FAILED(clipper->Initialize(bitmap.Get(), &crop)) ||
      FAILED(factory->CreateBitmapScaler(&scaler)) ||
      FAILED(scaler->Initialize(clipper.Get(), width, height, WICBitmapInterpolationModeFant))) {
    return {};
  }
  return CopyBgra(factory.Get(), scaler.Get()).value_or(Bgra{});
}

std::vector<uint8_t> EncodeJpeg(const Bgra& image, float quality) {
  auto factory = Factory();
  if (!factory || image.pixels.empty()) return {};
  ComPtr<IStream> stream;
  ComPtr<IWICBitmapEncoder> encoder;
  ComPtr<IWICBitmapFrameEncode> frame;
  ComPtr<IPropertyBag2> options;
  ComPtr<IWICBitmapSource> bgr;
  auto bitmap = Wrap(factory.Get(), image);
  if (!bitmap || FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) ||
      FAILED(factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder)) ||
      FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
      FAILED(encoder->CreateNewFrame(&frame, &options))) {
    return {};
  }
  PROPBAG2 option{};
  option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
  VARIANT value{};
  value.vt = VT_R4;
  value.fltVal = quality;
  options->Write(1, &option, &value);
  WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
  if (FAILED(frame->Initialize(options.Get())) || FAILED(frame->SetSize(image.width, image.height)) ||
      FAILED(frame->SetPixelFormat(&format)) || FAILED(WICConvertBitmapSource(format, bitmap.Get(), &bgr)) ||
      FAILED(frame->WriteSource(bgr.Get(), nullptr)) || FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
    return {};
  }
  STATSTG stat{};
  HGLOBAL global = nullptr;
  if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)) || FAILED(GetHGlobalFromStream(stream.Get(), &global))) return {};
  const auto* data = static_cast<const uint8_t*>(GlobalLock(global));
  if (!data) return {};
  std::vector<uint8_t> out(data, data + stat.cbSize.QuadPart);
  GlobalUnlock(global);
  return out;
}

}  // namespace webicam
