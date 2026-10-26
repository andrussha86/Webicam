# Webicam

iPhone как USB-веб-камера с низкой задержкой для Windows 11 — открытый аналог Camo.

- Дизайн: [docs/superpowers/specs/2026-10-27-webicam-core-pipeline-design.md](docs/superpowers/specs/2026-10-27-webicam-core-pipeline-design.md)
- Установка: [docs/install.md](docs/install.md)

## Сборка Windows-части

Нужны Visual Studio 2022 Build Tools (C++), CMake, Ninja и vcpkg (`VCPKG_ROOT`).
Из «Developer PowerShell for VS 2022»:

```powershell
cmake --preset default -S windows
cmake --build windows/build/default
ctest --test-dir windows/build/default --output-on-failure
```
