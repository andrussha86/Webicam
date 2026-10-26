# Webicam Core Pipeline — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** iPhone по USB становится веб-камерой «Webicam» в Windows 11 с задержкой < 100 мс (подпроект 1).

**Architecture:** iOS-приложение кодирует H.264 и отдаёт его по TCP (порт 47800, loopback); `webicam.exe` открывает туннель через usbmux (служба Apple, `127.0.0.1:27015`), декодирует на GPU, масштабирует в формат потребителя и пишет NV12 в кольцевой буфер общей памяти `Global\WebicamFrames-v1`, которую создаёт `webicam_vcam.dll`, загружаемая Frame Server через `MFCreateVirtualCamera`.

**Tech Stack:** Windows — C++23 (MSVC, CMake, vcpkg: GoogleTest, Dear ImGui), Media Foundation, D3D11, WRL. iOS — Swift 5.10+, SwiftUI, AVFoundation, VideoToolbox, Network.framework, XcodeGen. CI — GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-10-27-webicam-core-pipeline-design.md`

## Global Constraints

- Только Windows 11 (build ≥ 22000); минимальная iOS — 17.0.
- Кодек — только H.264 Annex B; без B-кадров; ключевой кадр раз в 2 с и по запросу.
- Порт приложения на телефоне — `47800`, слушать только loopback; usbmux — `127.0.0.1:27015`.
- Протокол: `type: u8 | length: u32 LE | payload`, все целые LE, `kProtocolVersion = 1`, максимум payload 16 МиБ (16 777 216 байт).
- Общая память: имя `Global\WebicamFrames-v1`, 3 слота по 3840×2160×3/2 байт, создаёт DLL, `.exe` только открывает.
- Имя виртуальной камеры — `Webicam`; CLSID источника — `{7C1D4E2A-5B3F-4E8A-9C21-6D0B8F3A1E57}`.
- Форматы, объявляемые камерой: 640×360, 1280×720, 1920×1080 @ 30 и 60; 3840×2160 @ 30.
- Битрейт по умолчанию (кбит/с): 720p30 8000, 720p60 12000, 1080p30 16000, 1080p60 25000, 4K30 45000.
- Heartbeat устаревает через 1000 мс; ping раз в 1 с; окно синхронизации часов — 10 замеров.
- Сертификаты и ключи подписи в репозитории не хранятся; `.ipa` собирается с `CODE_SIGNING_ALLOWED=NO`.
- Тексты UI — на русском.

## Review Focus

1. **Потребитель запросил формат, а свежий кадр ещё старого размера** (во время переключения) → DLL отдаёт кадр ровно запрошенного размера (заглушку), никогда не буфер другого размера. Тест: Task 3, `ChooseOutput_SizeMismatchGivesPlaceholder`.
2. **Кабель выдернули посреди сообщения** → частичное сообщение не публикуется как кадр, сессия сообщает `onClosed`, `.exe` не падает. Тест: Task 8, `DeviceSession_PartialMessageThenCloseReportsClosed`.
3. **`.exe` запущен, когда ни один потребитель не открыл камеру** (секции нет) → превью работает, `FramePublisher::TryAttach()` возвращает `false` без исключений и повторяется. Тест: Task 9, `FramePublisher_NoSectionReturnsFalse`.
4. **Не установлена служба Apple Mobile Device** → `UsbmuxError::ServiceUnavailable`, UI показывает подсказку, нет зависания. Тест: Task 5, `Usbmux_ServiceDownGivesServiceUnavailable`.
5. **Запущен второй экземпляр `webicam.exe`** → второй сообщает «Webicam уже запущен» и выходит, не трогая камеру первого. Тест: Task 10, `SingleInstance_SecondAcquireFails`.

---

## Файловая структура

```
.github/workflows/windows.yml        сборка + тесты Windows
.github/workflows/ios.yml            swift test + неподписанный .ipa
.github/workflows/release.yml        релиз по тегу v*
shared/protocol.md                   нормативный протокол + hex тест-векторов
shared/testvectors/*.bin             бинарные образцы сообщений
windows/CMakeLists.txt, CMakePresets.json, vcpkg.json
windows/core/include/webicam/
  shm_layout.h      константы и структуры общей памяти
  frame_ring.h      FrameRing (писатель/читатель без блокировок)
  shared_section.h  создание/открытие именованной секции с DACL
  vcam_logic.h      ChooseOutput — чистая логика выбора кадра для DLL
  protocol.h        сообщения, Encode, MessageParser
  clock_sync.h      ClockSync
  net.h             Socket (winsock RAII)
  plist.h           минимальный XML plist
  usbmux.h          UsbmuxClient
  h264_decoder.h    H264Decoder (MF + D3D11)
  frame_scaler.h    FrameScaler (D3D11 Video Processor)
  device_session.h  DeviceSession
  single_instance.h AcquireSingleInstance
windows/core/src/*.cpp               реализации (по одному файлу на заголовок)
windows/core/testing/test_encoder.{h,cpp}  TestPatternEncoder (MF H.264 encoder) — для тестов и fake-phone
windows/vcam/   dllmain.cpp, class_factory.*, media_source.*, media_stream.*, no_signal.*, webicam_vcam.def
windows/app/    main.cpp, pipeline.*, frame_publisher.*, virtual_cam_host.*, ui.*, connection_manager.*
windows/tools/vcam-probe/main.cpp    проверка виртуальной камеры без телефона (Task 3)
windows/tools/fake-phone/main.cpp    TCP-сервер, говорящий как телефон
windows/tools/gen-testvectors/main.cpp
windows/tests/*.cpp                  GoogleTest
windows/install.ps1, windows/uninstall.ps1
ios/project.yml
ios/WebicamCore/Package.swift, Sources/WebicamCore/*.swift, Tests/WebicamCoreTests/*.swift
ios/WebicamApp/*.swift, Info.plist
docs/install.md, docs/manual-test-checklist.md
```

---

### Task 1: Тулчейн и скелет Windows-сборки

**Files:**
- Create: `windows/CMakeLists.txt`, `windows/CMakePresets.json`, `windows/vcpkg.json`, `windows/core/CMakeLists.txt`, `windows/tests/CMakeLists.txt`, `windows/tests/smoke_test.cpp`, `.github/workflows/windows.yml`, `.gitignore`, `README.md`

**Interfaces:**
- Produces: CMake-цели `webicam_core` (static lib, include dir `windows/core/include`), `webicam_tests` (gtest, `gtest_discover_tests`); preset `default` (Ninja, MSVC x64, Debug) и `release`; `CMAKE_CXX_STANDARD 23`; линковка `mfplat mfuuid mfreadwrite mf d3d11 dxgi ws2_32 ole32 propsys` на `webicam_core`.

- [ ] **Step 1: Установить тулчейн на машину разработчика.** `winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.Windows11SDK.26100 --includeRecommended"`, `winget install Kitware.CMake Ninja-build.Ninja`, vcpkg: `git clone https://github.com/microsoft/vcpkg C:\vcpkg; C:\vcpkg\bootstrap-vcpkg.bat`, `setx VCPKG_ROOT C:\vcpkg`. Также «Apple Devices» из Microsoft Store. Проверка: из «Developer PowerShell for VS 2022» `cl` и `cmake --version` работают; `Get-Service *Apple*` показывает службу.
- [ ] **Step 2: Написать `smoke_test.cpp`** с `TEST(Smoke, CoreLinks) { EXPECT_EQ(webicam::kCoreVersion, 1); }`, где `kCoreVersion` объявлен в `windows/core/include/webicam/version.h`.
- [ ] **Step 3: Собрать и запустить.** `cmake --preset default -S windows; cmake --build windows/build/default; ctest --test-dir windows/build/default --output-on-failure` → `100% tests passed`.
- [ ] **Step 4: CI.** `windows.yml`: `windows-latest`, `lukka/run-vcpkg@v11` (manifest mode), `cmake --preset release`, build, ctest. `.gitignore`: `windows/build/`, `ios/*.xcodeproj`, `*.ipa`, `.DS_Store`.
- [ ] **Step 5: Commit** `chore: windows build skeleton and CI`.

---

### Task 2: Общая память и FrameRing

**Files:**
- Create: `windows/core/include/webicam/shm_layout.h`, `frame_ring.h`, `shared_section.h`, `windows/core/src/frame_ring.cpp`, `shared_section.cpp`
- Test: `windows/tests/frame_ring_test.cpp`, `windows/tests/shared_section_test.cpp`

**Interfaces:**
- Produces:
```cpp
namespace webicam::shm {
inline constexpr uint32_t kLayoutVersion = 1, kSlotCount = 3, kMaxWidth = 3840, kMaxHeight = 2160;
inline constexpr size_t   kSlotDataBytes = size_t(kMaxWidth) * kMaxHeight * 3 / 2;
inline constexpr wchar_t  kSectionName[] = L"Global\\WebicamFrames-v1";
// SY, LOCAL SERVICE, INTERACTIVE — полный доступ
inline constexpr wchar_t  kSectionSddl[] = L"D:(A;;GA;;;SY)(A;;GA;;;LS)(A;;GA;;;IU)";
}
namespace webicam {
struct FrameInfo { uint32_t width, height, stride; int64_t timestamp100ns; };
class FrameRing {
 public:
  static size_t RequiredBytes();
  static FrameRing Initialize(void* mem);           // создатель: обнуляет, пишет версию
  static std::optional<FrameRing> Attach(void* mem); // nullopt при несовпадении версии
  // писатель (.exe)
  bool Publish(const FrameInfo& info, std::span<const uint8_t> nv12); // false если не влезает
  void SetHeartbeat(uint64_t nowMs, bool connected);
  std::optional<std::array<uint32_t,3>> RequestedFormat() const;      // {w,h,fps}, nullopt если 0
  // читатель (DLL)
  // копирует свежий кадр если его generation > lastGeneration; возвращает новую generation
  std::optional<uint64_t> ReadLatest(uint64_t lastGeneration, FrameInfo& info, std::vector<uint8_t>& out);
  void SetRequestedFormat(uint32_t w, uint32_t h, uint32_t fps);
  uint64_t HeartbeatMs() const; bool Connected() const;
};
class SharedSection {
 public:
  static std::optional<SharedSection> Create(const wchar_t* name, size_t bytes, const wchar_t* sddl);
  static std::optional<SharedSection> Open(const wchar_t* name, size_t bytes);
  void* data() const; // move-only, RAII (UnmapViewOfFile + CloseHandle)
};
}
```

- [ ] **Step 1: Тесты FrameRing.**
  - `FrameRing_ReadAfterPublishReturnsFrame`: publish 64×32 (stride 64, данные `i % 251`), `ReadLatest(0, …)` → generation > 0, info совпадает, байты совпадают.
  - `FrameRing_ReadWithSameGenerationReturnsNullopt`.
  - `FrameRing_LatestWins`: publish A, B, C → читается C.
  - `FrameRing_OversizeRejected`: 3840×2161 → `Publish` == false.
  - `FrameRing_AttachRejectsWrongVersion`: испортить версию → `Attach` == nullopt.
  - `FrameRing_ConcurrentNoTornFrames`: писатель 2 с публикует кадры 1280×720, заполненные одним байтом `n % 256`, с `timestamp100ns = n`; читатель в другом потоке проверяет, что каждый прочитанный кадр однороден и байт == `timestamp100ns % 256`, а timestamps не убывают.
- [ ] **Step 2: Запустить** `ctest -R FrameRing` → FAIL (не компилируется).
- [ ] **Step 3: Реализовать.** Раскладка: `ControlBlock` (выровнен 64 байта: `layoutVersion`, `std::atomic<uint64_t> heartbeatMs`, `std::atomic<uint32_t> connected, requestedWidth, requestedHeight, requestedFps, readingSlot`, `std::atomic<uint64_t> latest` = `(generation << 8) | slot`), затем 3 × (`SlotHeader` с `std::atomic<uint64_t> generation` + данные). Алгоритм: писатель берёт слот ≠ `latest.slot` и ≠ `readingSlot`, ставит `slot.generation = 0`, пишет данные и заголовок, ставит `slot.generation = g`, публикует `latest`. Читатель: читает `latest`, пишет `readingSlot`, перечитывает `latest` (если сменился — повтор), копирует, проверяет `slot.generation == g`, иначе повтор (до 3 раз, затем nullopt).
- [ ] **Step 4: Тест SharedSection** `SharedSection_CreateThenOpenSharesMemory`: имя `Local\WebicamTest-<pid>`, `Create(..., 4096, kSectionSddl)`, записать байт, `Open` видит его; `Open` несуществующего → nullopt.
- [ ] **Step 5: Реализовать SharedSection** (`ConvertStringSecurityDescriptorToSecurityDescriptorW`, `CreateFileMappingW`, `OpenFileMappingW(FILE_MAP_ALL_ACCESS)`, `MapViewOfFile`).
- [ ] **Step 6: Запустить** `ctest` → PASS.
- [ ] **Step 7: Commit** `feat(core): shared memory frame ring`.

---

### Task 3: Виртуальная камера (DLL) + проверка рисков без телефона

Это шлюз проекта: если секция `Global\` из Frame Server не открывается из `.exe`, **остановиться** и сообщить человеку; запасной путь — именованный пайп (спец §6).

**Files:**
- Create: `windows/core/include/webicam/vcam_logic.h`, `windows/core/src/vcam_logic.cpp`, `windows/vcam/*` (см. структуру), `windows/app/virtual_cam_host.{h,cpp}`, `windows/tools/vcam-probe/main.cpp`, `windows/install.ps1`, `windows/uninstall.ps1`
- Test: `windows/tests/vcam_logic_test.cpp`

**Interfaces:**
- Consumes: `FrameRing`, `SharedSection`, `shm::*` (Task 2).
- Produces:
```cpp
namespace webicam {
inline constexpr wchar_t kVcamClsidString[] = L"{7C1D4E2A-5B3F-4E8A-9C21-6D0B8F3A1E57}";
inline constexpr wchar_t kVcamFriendlyName[] = L"Webicam";
inline constexpr uint64_t kHeartbeatTimeoutMs = 1000;
struct OutputFormat { uint32_t width, height, fps; };
std::span<const OutputFormat> AdvertisedFormats(); // 9 форматов из Global Constraints, порядок: 1080p30 первым
enum class OutputKind { Frame, NoSignalDimmed, Placeholder };
OutputKind ChooseOutput(uint64_t nowMs, uint64_t heartbeatMs, bool connected,
                        std::optional<FrameInfo> latest, uint32_t reqW, uint32_t reqH);
}
// windows/app/virtual_cam_host.h
class VirtualCamHost { public: bool Start(); void Stop(); ~VirtualCamHost(); }; // MFCreateVirtualCamera(Software, Session, CurrentUser, L"Webicam", kVcamClsidString, &KSCATEGORY_VIDEOCAMERA, 1, …)
```

- [ ] **Step 1: Тесты ChooseOutput.**
  - `ChooseOutput_FreshMatchingFrame` → `Frame` (heartbeat 500 мс назад, connected, 1920×1080 при запросе 1920×1080).
  - `ChooseOutput_StaleHeartbeatDims` → `NoSignalDimmed` (heartbeat 1001 мс назад, кадр есть, размер совпадает).
  - `ChooseOutput_DisconnectedDims` → `NoSignalDimmed`.
  - `ChooseOutput_NoFrameEverGivesPlaceholder` → `Placeholder`.
  - `ChooseOutput_SizeMismatchGivesPlaceholder` → `Placeholder` (кадр 1280×720, запрос 1920×1080) — Review Focus 1.
  - `AdvertisedFormats_HasNineAndNo4K60`.
- [ ] **Step 2: Запустить** → FAIL; **реализовать** `vcam_logic.cpp`; **запустить** → PASS.
- [ ] **Step 3: Реализовать DLL.** Структуру брать из `smourier/VCamSample` (MIT) и `microsoft/Windows-Camera/Samples/VirtualCamera`: `MediaSource` (`IMFMediaSourceEx`, `IMFGetService`, `IKsControl`, `IMFSampleAllocatorControl`), `MediaStream` (`IMFMediaStream2`, `IKsControl`) с `MF_DEVICESTREAM_STREAM_CATEGORY = PINNAME_VIDEO_CAPTURE`, `MF_DEVICESTREAM_FRAMESERVER_SHARED = 1`, по одному NV12 media type на каждый `AdvertisedFormats()`; `SetCurrentMediaType` с неизвестным типом → `MF_E_INVALIDMEDIATYPE`. В `Start` источник делает `SharedSection::Create(kSectionName, FrameRing::RequiredBytes(), kSectionSddl)` (если уже есть — `Open`), пишет `SetRequestedFormat`. В `RequestSample`: `ReadLatest` → `ChooseOutput` → `Frame`: копия; `NoSignalDimmed`: последний кадр с Y×0.4 и наложенной надписью «Нет сигнала»; `Placeholder`: Y=16/UV=128 с надписью «Webicam: телефон не подключён». Надписи рендерить один раз на размер через GDI (`CreateDIBSection` + `DrawTextW`, Segoe UI) в маску яркости (`no_signal.cpp`). Перед копированием проверять `width ≤ kMaxWidth`, `height ≤ kMaxHeight`, `stride*height*3/2 ≤ kSlotDataBytes`. `DllRegisterServer` пишет `HKLM\Software\Classes\CLSID\{…}\InprocServer32` (`ThreadingModel=Both`).
- [ ] **Step 4: `install.ps1`** (требует админа: проверка `[Security.Principal.WindowsPrincipal]`, `regsvr32 /s webicam_vcam.dll` из папки скрипта), `uninstall.ps1` (`regsvr32 /u /s`).
- [ ] **Step 5: `vcam-probe`.** `VirtualCamHost::Start()`, затем цикл 30 FPS: раз в 500 мс пробует `SharedSection::Open`, после успеха `FrameRing::Attach`, публикует запрошенный (`RequestedFormat`) размер с движущимися цветными полосами, `SetHeartbeat(now, true)`. Флаг `--stop-heartbeat-after 5` перестаёт обновлять heartbeat через 5 с.
- [ ] **Step 6: Ручная проверка рисков** (запустить `install.ps1` от админа, затем `vcam-probe`):
  - приложение «Камера» показывает камеру «Webicam» с движущимися полосами;
  - в OBS (Video Capture Device → Webicam, разрешение 1280×720) — полосы в 1280×720;
  - `vcam-probe --stop-heartbeat-after 5` → через ~6 с затемнённая картинка с «Нет сигнала»;
  - до открытия камеры потребителем probe пишет «waiting for section», после — «attached».
  Если хоть одно не так — стоп, отчёт человеку.
- [ ] **Step 7: Commit** `feat(vcam): Media Foundation virtual camera and probe`.

---

### Task 4: Протокол (C++) и тест-векторы

**Files:**
- Create: `shared/protocol.md`, `windows/core/include/webicam/protocol.h`, `windows/core/src/protocol.cpp`, `windows/tools/gen-testvectors/main.cpp`, `shared/testvectors/*.bin`
- Test: `windows/tests/protocol_test.cpp`

**Interfaces:**
- Produces:
```cpp
namespace webicam::proto {
inline constexpr uint16_t kProtocolVersion = 1, kPhonePort = 47800;
inline constexpr uint32_t kMaxPayloadBytes = 16u * 1024 * 1024;
enum class MsgType : uint8_t { Hello = 1, Configure, VideoFrame, RequestKeyframe, Ping, Pong, Status };
struct CaptureFormat { uint16_t width, height; uint8_t maxFps; auto operator<=>(const CaptureFormat&) const = default; };
struct Hello { uint16_t version = kProtocolVersion; std::string model; std::vector<CaptureFormat> formats; }; // ПК шлёт только version
struct Configure { uint16_t width, height; uint8_t fps; uint32_t bitrateKbps; };
struct VideoFrame { uint64_t captureTimeUs; bool keyframe; std::vector<uint8_t> annexB; };
struct RequestKeyframe {};
struct Ping { uint64_t pcTimeUs; };
struct Pong { uint64_t pcTimeUs, phoneTimeUs; };
enum class StreamState : uint8_t { Streaming = 0, StoppedByUser = 1, ThermalThrottled = 2, Error = 3 };
struct Status { StreamState state; uint8_t thermal; std::string message; }; // thermal: 0 nominal,1 fair,2 serious,3 critical
using Message = std::variant<Hello, Configure, VideoFrame, RequestKeyframe, Ping, Pong, Status>;
std::vector<uint8_t> Encode(const Message&);
class MessageParser {
 public:
  enum class Result { NeedMore, Message, Error };
  void Feed(std::span<const uint8_t> bytes);
  Result Next(Message& out); // Error — навсегда (неизвестный тип, length > kMaxPayloadBytes, битый payload)
  size_t Buffered() const;
};
uint32_t DefaultBitrateKbps(uint16_t height, uint8_t fps); // таблица из Global Constraints; иначе 16000
}
```
Hello payload: `version: u16`, далее (только если payload длиннее 2 байт) `model: str`, `formats: count u16 + {w u16, h u16, fps u8}`. Зафиксировать это в `protocol.md`.

- [ ] **Step 1: Тесты.** Round-trip для каждого типа; `Parser_ByteByByteFeedYieldsSameMessages`; `Parser_TwoMessagesInOneFeed`; `Parser_OversizeLengthIsError` (length = 16 777 217); `Parser_UnknownTypeIsError` (type 0x09); `Parser_TruncatedStringIsError`; `DefaultBitrate_Table` (1080/60 → 25000, 2160/30 → 45000, 720/30 → 8000); `Vectors_MatchFiles`: для каждого вектора ниже `Encode(value) == file bytes` и парсинг файла == value (путь через `WEBICAM_TESTVECTORS_DIR`, задаётся в CMake).
- [ ] **Step 2: Запустить** → FAIL. **Реализовать** `protocol.cpp`.
- [ ] **Step 3: Тест-векторы.** `gen-testvectors <dir>` пишет:
  `hello_pc.bin` {1}; `hello_phone.bin` {1, "iPhone16,1", [1280×720@60, 1920×1080@60, 3840×2160@30]}; `configure_1080p60.bin` {1920,1080,60,25000}; `video_frame_key.bin` {123456789, true, `00 00 00 01 67 42 00 1F`}; `request_keyframe.bin`; `ping.bin` {1000}; `pong.bin` {1000, 2000}; `status_thermal.bin` {ThermalThrottled, 2, "serious"}. Сгенерировать, вписать hex каждого файла в `protocol.md`, сверить руками первые два с описанием формата.
- [ ] **Step 4: Запустить** `ctest` → PASS.
- [ ] **Step 5: Commit** `feat(protocol): wire protocol, parser and shared test vectors`.

---

### Task 5: Сокет, plist и UsbmuxClient

**Files:**
- Create: `windows/core/include/webicam/net.h`, `plist.h`, `usbmux.h` + `src/*.cpp`
- Test: `windows/tests/plist_test.cpp`, `windows/tests/usbmux_test.cpp`, `windows/tests/fake_usbmuxd.{h,cpp}`

**Interfaces:**
- Produces:
```cpp
namespace webicam {
class Socket { // move-only, RAII, WSAStartup один раз на процесс
 public:
  static std::optional<Socket> ConnectTcp(const char* host, uint16_t port, int timeoutMs);
  static std::optional<Socket> Listen(const char* host, uint16_t port); Socket Accept();
  bool SendAll(std::span<const uint8_t>);
  int Recv(std::span<uint8_t>, int timeoutMs); // >0 байт, 0 закрыт, -1 ошибка, -2 таймаут
  void Shutdown(); uint16_t LocalPort() const;
};
struct PlistValue { /* kind: Integer|String|Dict|Array; int64_t integer; std::string str;
                       std::vector<std::pair<std::string, PlistValue>> dict; std::vector<PlistValue> array; */
  const PlistValue* Find(std::string_view key) const; };
std::string ToXml(const PlistValue&);
std::optional<PlistValue> ParseXml(std::string_view);
struct UsbmuxDevice { uint32_t deviceId; std::string serial; std::string connectionType; };
enum class UsbmuxError { ServiceUnavailable, DeviceNotFound, ConnectionRefused, Protocol };
class UsbmuxClient {
 public:
  explicit UsbmuxClient(uint16_t servicePort = 27015);
  std::expected<std::vector<UsbmuxDevice>, UsbmuxError> ListDevices();  // только ConnectionType == "USB"
  std::expected<Socket, UsbmuxError> Connect(uint32_t deviceId, uint16_t devicePort);
};
}
```
Usbmux: заголовок 16 байт LE `{length (включая заголовок), version = 1, message = 8 (plist), tag}`; запрос — dict `MessageType`, `ClientVersionString = "webicam"`, `ProgName = "webicam"`; `Connect` содержит `DeviceID` и `PortNumber = htons(port)` как integer; ответ `Result`/`Number`: 0 ок, 2 → `DeviceNotFound`, 3 → `ConnectionRefused`, иное → `Protocol`. После `Number = 0` сокет становится сырым туннелем. Отслеживание подключения — опрос `ListDevices` раз в секунду (вместо `Listen`; поведение для пользователя то же).

- [ ] **Step 1: Тесты plist.** Round-trip dict с integer/string/вложенным dict/array; разбор реального ответа `ListDevices` (образец ниже) → один девайс `DeviceID = 3`, `SerialNumber = "00008130-001A2B3C4D5E6F7A"`, `ConnectionType = "USB"`; мусор → nullopt.
```xml
<?xml version="1.0" encoding="UTF-8"?><!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd"><plist version="1.0"><dict><key>DeviceList</key><array><dict><key>DeviceID</key><integer>3</integer><key>MessageType</key><string>Attached</string><key>Properties</key><dict><key>ConnectionType</key><string>USB</string><key>DeviceID</key><integer>3</integer><key>SerialNumber</key><string>00008130-001A2B3C4D5E6F7A</string></dict></dict></array></dict></plist>
```
- [ ] **Step 2: Тесты usbmux** против `FakeUsbmuxd` (слушает `127.0.0.1:0`, отвечает по сценарию):
  - `Usbmux_ListDevicesParsesUsbOnly` (в списке USB и Network → один);
  - `Usbmux_ConnectSendsBigEndianPort` (fake проверяет `PortNumber == 0xB8BA` для 47800, отвечает 0, затем эхо: клиент шлёт «ping», получает «ping»);
  - `Usbmux_ConnectRefusedMapsError` (Number 3 → `ConnectionRefused`);
  - `Usbmux_ServiceDownGivesServiceUnavailable` (порт, на котором никто не слушает, ответ < 2 с) — Review Focus 4.
- [ ] **Step 3: Запустить** → FAIL; **реализовать**; **запустить** → PASS.
- [ ] **Step 4: Commit** `feat(core): usbmux client`.

---

### Task 6: ClockSync

**Files:**
- Create: `windows/core/include/webicam/clock_sync.h`, `src/clock_sync.cpp`
- Test: `windows/tests/clock_sync_test.cpp`

**Interfaces:**
- Produces:
```cpp
class ClockSync {
 public:
  void AddSample(uint64_t pcSendUs, uint64_t phoneUs, uint64_t pcRecvUs); // окно 10 последних
  std::optional<int64_t> OffsetUs() const; // pcTime ≈ phoneTime + offset; по замеру с минимальным RTT:
                                           // offset = (pcSendUs + pcRecvUs)/2 − phoneUs
};
```
- [ ] **Step 1: Тесты.** Пусто → nullopt; один замер (1000, 500, 1200) → 600; из замеров с RTT 200/50/900 выбирается RTT 50; 11-й замер вытесняет первый (если лучший был первым — смещение меняется).
- [ ] **Step 2: FAIL → реализовать → PASS. Commit** `feat(core): clock sync`.

---

### Task 7: Тестовый кодер и аппаратный H.264-декодер

**Files:**
- Create: `windows/core/testing/test_encoder.{h,cpp}` (цель `webicam_testing`), `windows/core/include/webicam/h264_decoder.h`, `src/h264_decoder.cpp`
- Test: `windows/tests/h264_decoder_test.cpp`

**Interfaces:**
- Produces:
```cpp
// test_encoder.h — кадры с однородной яркостью Y = 16 + (n*7) % 200, UV = 128
class TestPatternEncoder {
 public:
  static std::unique_ptr<TestPatternEncoder> Create(uint32_t w, uint32_t h, uint32_t fps, uint32_t bitrateKbps);
  std::vector<uint8_t> EncodeNext(bool forceKeyframe, uint64_t& frameIndex); // Annex B, может вернуть пусто
};
struct D3DContext { Microsoft::WRL::ComPtr<ID3D11Device> device; Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
                    static std::optional<D3DContext> Create(); }; // BGRA + VIDEO_SUPPORT, multithread protected
struct DecodedFrame { Microsoft::WRL::ComPtr<ID3D11Texture2D> texture; UINT subresource; uint32_t width, height; int64_t timestamp100ns; };
class H264Decoder {
 public:
  static std::unique_ptr<H264Decoder> Create(const D3DContext&);
  enum class Result { Frame, NeedMore, Error };
  Result Decode(std::span<const uint8_t> annexB, int64_t timestamp100ns, DecodedFrame& out);
  void Reset(); // после ошибки: сбросить и ждать ключевой кадр
};
```
Кодер: MF H.264 encoder MFT, `CODECAPI_AVLowLatencyMode = TRUE`, `CODECAPI_AVEncMPVDefaultBPictureCount = 0`, `CODECAPI_AVEncMPVGOPSize = fps*2`. Декодер: `CLSID_MSH264DecoderMFT` с `MF_SA_D3D11_AWARE`, `IMFDXGIDeviceManager`, `CODECAPI_AVLowLatencyMode = TRUE`, выход NV12; обработка `MF_E_TRANSFORM_STREAM_CHANGE`. Размер — из `MF_MT_MINIMUM_DISPLAY_APERTURE` (иначе 1080p вернётся как 1088).

- [ ] **Step 1: Тесты.**
  - `Decoder_DecodesTestPattern`: 1280×720@30, 60 кадров → ≥ 58 `Frame`, каждый 1280×720; яркость центра (через staging-копию) кадра n в ±8 от `16 + (n*7) % 200`.
  - `Decoder_1080pReportsDisplaySize` → height == 1080.
  - `Decoder_GarbageReturnsErrorThenRecoversOnKeyframe`: 64 байта мусора → `Error`; `Reset()`; ключевой кадр → `Frame`.
  - `Decoder_LowLatencyNoFrameDelay`: после подачи первого ключевого кадра `Decode` сразу возвращает `Frame` (нет буферизации).
- [ ] **Step 2: FAIL → реализовать → PASS. Commit** `feat(core): hardware H.264 decoder`.

---

### Task 8: FrameScaler и DeviceSession

**Files:**
- Create: `windows/core/include/webicam/frame_scaler.h`, `device_session.h` + `src/*.cpp`
- Test: `windows/tests/frame_scaler_test.cpp`, `windows/tests/device_session_test.cpp`

**Interfaces:**
- Consumes: `D3DContext`, `DecodedFrame` (Task 7); `proto::*` (Task 4); `Socket` (Task 5); `ClockSync` (Task 6).
- Produces:
```cpp
class FrameScaler { // D3D11 Video Processor; aspect-fill, обрезка по центру
 public:
  static std::unique_ptr<FrameScaler> Create(const D3DContext&);
  bool ToNv12Cpu(const DecodedFrame& in, uint32_t outW, uint32_t outH, std::vector<uint8_t>& out); // stride = outW
  ID3D11ShaderResourceView* ToPreview(const DecodedFrame& in, uint32_t maxW, uint32_t maxH);      // BGRA, вписано
};
class DeviceSession { // свой поток чтения + ping раз в 1 с
 public:
  struct Events {
    std::function<void(const proto::Hello&)> onHello;
    std::function<void(proto::VideoFrame&&)> onFrame;
    std::function<void(const proto::Status&)> onStatus;
    std::function<void(std::string reason)> onClosed; // вызывается ровно один раз
  };
  DeviceSession(Socket socket, Events events);
  ~DeviceSession(); // Shutdown + join
  void Start();      // шлёт Hello{kProtocolVersion}
  void SendConfigure(const proto::Configure&);
  void RequestKeyframe();
  std::optional<int64_t> PhoneToPcUs(uint64_t phoneUs) const;
};
uint64_t NowUs(); // QueryPerformanceCounter
```
- [ ] **Step 1: Тесты scaler.** Кадр из `TestPatternEncoder`+`H264Decoder` 1920×1080 → `ToNv12Cpu(1280, 720)` → размер буфера 1280×720×3/2, центр Y в ±8 от ожидаемого; 1920×1080 → 1080×1080 (1:1) не падает и центр сохраняется.
- [ ] **Step 2: Тесты сессии** (телефон — сокет в тесте, `Socket::Listen("127.0.0.1", 0)`):
  - `DeviceSession_HandshakeDeliversHello` (ПК шлёт `Hello{1}` первым, телефон отвечает `hello_phone` → `onHello` с 3 форматами);
  - `DeviceSession_VersionMismatchCloses` (Hello{2} → `onClosed` с текстом, содержащим «версия»);
  - `DeviceSession_FramesDelivered` (3 `VideoFrame` → 3 `onFrame` по порядку);
  - `DeviceSession_PingPongGivesOffset` (тест-телефон отвечает `Pong{pc, pc − 5000}` → `PhoneToPcUs(0)` ≈ 5000 ± 2000);
  - `DeviceSession_PartialMessageThenCloseReportsClosed` (половина `VideoFrame`, затем закрыть сокет → `onClosed`, `onFrame` не вызывался) — Review Focus 2.
- [ ] **Step 3: FAIL → реализовать → PASS. Commit** `feat(core): frame scaler and device session`.

---

### Task 9: Конвейер `.exe`, fake-phone и режим `--direct`

**Files:**
- Create: `windows/app/pipeline.{h,cpp}`, `windows/app/frame_publisher.{h,cpp}`, `windows/tools/fake-phone/main.cpp`
- Test: `windows/tests/frame_publisher_test.cpp`

**Interfaces:**
- Consumes: всё из Tasks 2–8.
- Produces:
```cpp
class FramePublisher {
 public:
  bool TryAttach();  // SharedSection::Open(kSectionName) + FrameRing::Attach; false без исключений
  bool Attached() const;
  std::optional<OutputFormat> Requested() const;
  bool Publish(std::span<const uint8_t> nv12, uint32_t w, uint32_t h, int64_t ts100ns);
  void Heartbeat(bool phoneConnected);
};
struct PipelineStats { double fps; double latencyMs; uint64_t droppedFrames; uint32_t width, height; };
class Pipeline { // принимает VideoFrame из DeviceSession
 public:
  Pipeline(D3DContext, std::function<void()> requestKeyframe, std::function<std::optional<int64_t>(uint64_t)> phoneToPcUs);
  void OnFrame(proto::VideoFrame&&);   // decode → (TryAttach раз в 500 мс) → scale в Requested → Publish
  void OnConnectionChanged(bool connected);
  ID3D11ShaderResourceView* Preview(); PipelineStats Stats() const;
};
```
Пока не пришёл ключевой кадр (старт или после `Error`) — кадры отбрасываются и считаются в `droppedFrames`; при `Error` вызывается `requestKeyframe`. Задержка = `NowUs()` после `Publish` − `phoneToPcUs(captureTimeUs)`, скользящее среднее за 1 с.

`fake-phone [--port 47800] [--width 1920 --height 1080 --fps 60]`: слушает TCP, делает рукопожатие как телефон (`hello_phone`-форматы), на `Configure` перенастраивает `TestPatternEncoder`, на `RequestKeyframe` — ключевой кадр, отвечает на `Ping`, `captureTimeUs = NowUs()`.

- [ ] **Step 1: Тест** `FramePublisher_NoSectionReturnsFalse` (секции нет → `TryAttach()==false`, `Publish` → false, без исключений) — Review Focus 3.
- [ ] **Step 2: FAIL → реализовать FramePublisher, Pipeline, fake-phone → PASS.**
- [ ] **Step 3: Временный `main.cpp` с `--direct host:port`** (без UI): `VirtualCamHost::Start`, `DeviceSession` к fake-phone, `Configure` 1920×1080@60 и `DefaultBitrateKbps`, раз в секунду печатать `PipelineStats`.
- [ ] **Step 4: Ручная проверка.** `fake-phone` + `webicam.exe --direct 127.0.0.1:47800`; в OBS камера Webicam 1920×1080 показывает мигающую по яркости картинку; печатаемая задержка < 30 мс; убить fake-phone → в OBS «Нет сигнала» ≤ 2 с; перезапустить fake-phone → картинка возвращается (переподключение — в Task 10, здесь достаточно перезапуска `.exe`).
- [ ] **Step 5: Commit** `feat(app): decode-scale-publish pipeline and fake phone`.

---

### Task 10: Приложение: подключение, UI, трей

**Files:**
- Create: `windows/core/include/webicam/single_instance.h`, `src/single_instance.cpp`, `windows/app/connection_manager.{h,cpp}`, `windows/app/ui.{h,cpp}`
- Modify: `windows/app/main.cpp` (окно Win32 + D3D11 swapchain + ImGui; `--direct` сохраняется)
- Test: `windows/tests/single_instance_test.cpp`, `windows/tests/connection_manager_test.cpp`

**Interfaces:**
- Produces:
```cpp
std::optional<HANDLE> AcquireSingleInstance(const wchar_t* name); // L"Local\\WebicamApp-SingleInstance"
enum class ConnState { AppleServiceMissing, NoDevice, ChooseDevice, PhoneAppNotOpen, Connecting, Streaming, VersionMismatch, Error };
class ConnectionManager { // поток: опрос ListDevices раз в 1 с, Connect(…, 47800) раз в 1.5 с, переподключение
 public:
  ConnectionManager(std::function<std::unique_ptr<DeviceSession>(Socket)> makeSession, uint16_t usbmuxPort = 27015);
  ConnState State() const; std::string Detail() const;           // текст для UI
  std::vector<UsbmuxDevice> Devices() const; void SelectDevice(uint32_t deviceId);
  void SetDirect(std::string host, uint16_t port);                 // режим --direct
  std::vector<proto::CaptureFormat> PhoneFormats() const;          // из Hello
};
```
Тексты состояний (UI, дословно): `AppleServiceMissing` — «Установите „Apple Devices“ из Microsoft Store»; `NoDevice` — «Подключите iPhone кабелем»; `PhoneAppNotOpen` — «Откройте Webicam на iPhone» (для `ConnectionRefused`; если iPhone не доверяет ПК, usbmux тоже даёт отказ — добавить «Разблокируйте iPhone и нажмите „Доверять“»); `VersionMismatch` — «Версии приложений не совпадают: обновите …»; `Streaming` — «Подключено: <model>».

- [ ] **Step 1: Тесты.** `SingleInstance_SecondAcquireFails` (Review Focus 5); `ConnectionManager_ServiceMissingState` (никто не слушает usbmux-порт → `AppleServiceMissing` за ≤ 2 с); `ConnectionManager_RefusedMeansPhoneAppNotOpen` (FakeUsbmuxd из Task 5 отвечает Number 3); `ConnectionManager_AutoSelectsSingleDevice`; `ConnectionManager_ReconnectsAfterClose` (direct-режим к тестовому серверу: закрыть соединение → через ≤ 3 с снова `Streaming`).
- [ ] **Step 2: FAIL → реализовать → PASS.**
- [ ] **Step 3: UI (ImGui).** Слева: статус-строка (`Detail()`), список устройств при `ChooseDevice`, комбобокс «Разрешение захвата» из `PhoneFormats()` × {30, 60 ≤ maxFps}, поле «Битрейт, Мбит/с» (по умолчанию `DefaultBitrateKbps`), кнопка «Применить» → `SendConfigure`. Справа: превью `Pipeline::Preview()`. Внизу: FPS, задержка (мс), потерянные кадры, «Выход камеры: W×H@fps» из `Requested()`. Закрытие окна → в трей (`Shell_NotifyIcon`, меню «Открыть» / «Выход»). При старте — `AcquireSingleInstance`, иначе `MessageBox` «Webicam уже запущен» и выход.
- [ ] **Step 4: Ручная проверка с fake-phone** (`--direct`): смена разрешения на 1280×720@30 в UI → fake-phone получает `Configure`, OBS продолжает показывать без перевыбора; закрыть окно → трей, камера работает; «Выход» → камера пропадает.
- [ ] **Step 5: Commit** `feat(app): connection manager, UI and tray`.

---

### Task 11: WebicamCore (Swift Package)

**Files:**
- Create: `ios/WebicamCore/Package.swift` (iOS 17, macOS 14), `Sources/WebicamCore/Protocol.swift`, `MessageParser.swift`, `AnnexB.swift`, `Bitrate.swift`, `Tests/WebicamCoreTests/ProtocolTests.swift`, `AnnexBTests.swift`, `.github/workflows/ios.yml` (job `core-tests`)

**Interfaces:**
- Produces (зеркало Task 4):
```swift
public enum ProtocolConstants { public static let version: UInt16 = 1; public static let phonePort: UInt16 = 47800; public static let maxPayloadBytes = 16 * 1024 * 1024 }
public struct CaptureFormat: Equatable { public var width: UInt16; public var height: UInt16; public var maxFps: UInt8 }
public enum StreamState: UInt8 { case streaming = 0, stoppedByUser, thermalThrottled, error }
public enum Message: Equatable {
  case hello(version: UInt16, model: String?, formats: [CaptureFormat])
  case configure(width: UInt16, height: UInt16, fps: UInt8, bitrateKbps: UInt32)
  case videoFrame(captureTimeUs: UInt64, keyframe: Bool, annexB: Data)
  case requestKeyframe, ping(pcTimeUs: UInt64), pong(pcTimeUs: UInt64, phoneTimeUs: UInt64)
  case status(state: StreamState, thermal: UInt8, message: String)
  public func encoded() -> Data
}
public struct MessageParser { public init(); public mutating func feed(_ data: Data); public mutating func next() throws -> Message? } // throws ProtocolError
public enum AnnexB { public static func fromAVCC(_ avcc: Data, nalLengthSize: Int = 4, parameterSets: [Data]?) -> Data } // parameterSets → в начало с 00 00 00 01
public func defaultBitrateKbps(height: UInt16, fps: UInt8) -> UInt32
```
- [ ] **Step 1: Тесты.** Каждый файл `shared/testvectors/*.bin` (путь от `#filePath`: `../../../../shared/testvectors`) разбирается в ожидаемое значение из Task 4 Step 3 и `encoded()` даёт те же байты; oversize length → throws; `AnnexB`: AVCC `00 00 00 02 AA BB 00 00 00 01 CC` с SPS `67 42` и PPS `68 CE` → `00 00 00 01 67 42 00 00 00 01 68 CE 00 00 00 01 AA BB 00 00 00 01 CC`; `defaultBitrateKbps(1080, 60) == 25000`.
- [ ] **Step 2: CI** `ios.yml` job `core-tests`: `macos-latest`, `swift test --package-path ios/WebicamCore`. Push в ветку → job FAIL (тесты без реализации).
- [ ] **Step 3: Реализовать → push → job PASS.** (Локально на Windows проверить нельзя; ждать CI, `gh run watch` после установки `gh` либо смотреть вкладку Actions.)
- [ ] **Step 4: Commit** `feat(ios): WebicamCore protocol package`.

---

### Task 12: iOS-приложение и сборка `.ipa`

**Files:**
- Create: `ios/project.yml`, `ios/WebicamApp/WebicamApp.swift`, `CaptureEngine.swift`, `Encoder.swift`, `StreamServer.swift`, `ControlHandler.swift`, `StatusView.swift`, `Info.plist`; Modify: `.github/workflows/ios.yml` (job `build-ipa`)

**Interfaces:**
- Consumes: `WebicamCore` (Task 11).
- Produces: таргет `Webicam` (bundle id `com.webicam.app`, deployment 17.0, iPhone only); артефакт CI `Webicam.ipa`.
```swift
final class CaptureEngine { var onFrame: ((CVPixelBuffer, CMTime) -> Void)?; var supportedFormats: [CaptureFormat] { get } // из основной wide-камеры, 30/60, ≤ 4K
                            func configure(width: Int, height: Int, fps: Int) throws; func start(); func stop() }
final class Encoder { var onEncoded: ((Data /*Annex B*/, Bool /*keyframe*/, UInt64 /*captureTimeUs*/) -> Void)?
                      func configure(width: Int, height: Int, fps: Int, bitrateKbps: Int) throws; func encode(_ pb: CVPixelBuffer, time: CMTime); func forceKeyframe() }
final class StreamServer { var onMessage: ((Message) -> Void)?; var onConnectionChange: ((Bool) -> Void)?; func start() throws; func send(_ m: Message) }
final class ControlHandler { init(capture: CaptureEngine, encoder: Encoder, server: StreamServer) } // связывает всё
```
- Encoder: `VTCompressionSessionCreate` с `encoderSpecification [kVTVideoEncoderSpecification_EnableLowLatencyRateControl: true]`; `RealTime = true`, `AllowFrameReordering = false`, `ProfileLevel = H264_High_AutoLevel`, `MaxKeyFrameIntervalDuration = 2`, `AverageBitRate`, `ExpectedFrameRate`; если предыдущий кадр ещё кодируется — новый отбрасывается; выход → `AnnexB.fromAVCC` (на ключевых кадрах с SPS/PPS из format description). `captureTimeUs` — из `CMTime` захвата (часы `CMClockGetHostTimeClock`, в мкс).
- StreamServer: `NWListener(using: .tcp, on: 47800)` с `parameters.requiredLocalEndpoint = .hostPort(host: "127.0.0.1", port: 47800)`; новый клиент закрывает старого; первым шлёт `hello(version:1, model: <hw.machine>, formats: capture.supportedFormats)`. Если usbmux-соединения не доходят до loopback-сокета (проверка в Step 3) — снять ограничение интерфейса и сообщить человеку.
- ControlHandler: `configure` → перенастроить захват и кодер, следующий кадр ключевой; `requestKeyframe` → `forceKeyframe`; `ping` → `pong(pcTimeUs, nowUs)` по тем же часам; `ProcessInfo.thermalStateDidChangeNotification`: `.serious` → fps 30 и битрейт ×0.7, `status(.thermalThrottled, 2, "serious")`; `.critical` → `status(.error, 3, …)`.
- StatusView: «Ожидание ПК…» / «Подключено к ПК», `W×H @ fps`, битрейт, кнопка «Стоп» (останавливает захват, шлёт `status(.stoppedByUser…)`; повторное нажатие «Старт»), яркость экрана 0.1 после 10 с без касаний, `isIdleTimerDisabled = true`. Info.plist: `NSCameraUsageDescription` = «Webicam передаёт видео с камеры на компьютер».
- Ориентация вывода — landscape (`videoRotationAngle = 0`); портрет и кроп — подпроект 2.

- [ ] **Step 1: CI job `build-ipa`.** `brew install xcodegen` → `cd ios && xcodegen` → `xcodebuild -project Webicam.xcodeproj -scheme Webicam -configuration Release -sdk iphoneos -destination 'generic/platform=iOS' CODE_SIGNING_ALLOWED=NO build` → `mkdir Payload && cp -R …/Webicam.app Payload/ && zip -r Webicam.ipa Payload` → `actions/upload-artifact`. Push → артефакт `Webicam.ipa` есть.
- [ ] **Step 2: Установить через Sideloadly** (Apple ID, iPhone по USB), на iPhone включить «Режим разработчика» и доверие профилю. Приложение открывается, просит доступ к камере, показывает «Ожидание ПК…».
- [ ] **Step 3: Сквозная проверка** с `webicam.exe` (без `--direct`): статус «Подключено: iPhone16,1», в OBS живое видео 1080p60, в UI задержка до общей памяти отображается; «Стоп» на телефоне → «Нет сигнала» в OBS.
- [ ] **Step 4: Commit** `feat(ios): capture, encode and stream app`.

---

### Task 13: Релиз, документация, приёмка

**Files:**
- Create: `.github/workflows/release.yml`, `docs/install.md`, `docs/manual-test-checklist.md`; Modify: `README.md`

- [ ] **Step 1: `release.yml`** по тегу `v*`: переиспользует сборку Windows (zip: `webicam.exe`, `webicam_vcam.dll`, `install.ps1`, `uninstall.ps1`) и `build-ipa`, публикует GitHub Release (`softprops/action-gh-release@v2`) с `Webicam-windows-x64.zip` и `Webicam.ipa`.
- [ ] **Step 2: `docs/install.md`** (по-русски): Windows 11 + «Apple Devices» → распаковать zip → `install.ps1` от администратора → Sideloadly + Apple ID → «Режим разработчика» → доверие ПК → запуск. Отдельно: подпись бесплатным Apple ID живёт 7 дней.
- [ ] **Step 3: `docs/manual-test-checklist.md`** — пункты из спец §9 и критерии §10, включая замер задержки (миллисекундный таймер на мониторе, iPhone снимает его, вывод в OBS на том же мониторе, разница на скриншоте; 5 замеров, медиана).
- [ ] **Step 4: Пройти чек-лист на реальном iPhone 15 Pro**, вписать результаты (медиана задержки 1080p60, 30-минутный прогон, выдёргивание кабеля, два потребителя одновременно, «Камера»/Zoom/Chrome/OBS).
- [ ] **Step 5: Commit** `docs: install guide, test checklist and release workflow`; тег `v0.1.0` — только после согласия человека.
