# Registers the Webicam virtual camera (run once, as Administrator).
# Frame Server loads the DLL as LOCAL SERVICE, which cannot read user profile
# folders, so the DLL is copied to Program Files first.
param([string]$Source = $PSScriptRoot)

$principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "Запустите install.ps1 от имени администратора."
    exit 1
}
$dll = Join-Path $Source "webicam_vcam.dll"
if (-not (Test-Path $dll)) { Write-Error "Не найден $dll"; exit 1 }

$target = Join-Path $env:ProgramFiles "Webicam"
New-Item -ItemType Directory -Force $target | Out-Null
$installed = Join-Path $target "webicam_vcam.dll"
if (Test-Path $installed) { & regsvr32.exe /u /s $installed }
Copy-Item $dll $installed -Force
& regsvr32.exe /s $installed
if ($LASTEXITCODE -ne 0) { Write-Error "regsvr32 завершился с кодом $LASTEXITCODE"; exit $LASTEXITCODE }
Write-Host "Webicam: виртуальная камера зарегистрирована ($installed)."
