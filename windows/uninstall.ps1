# Unregisters the Webicam virtual camera (run as Administrator).
$installed = Join-Path (Join-Path $env:ProgramFiles "Webicam") "webicam_vcam.dll"
if (Test-Path $installed) {
    & regsvr32.exe /u /s $installed
    Remove-Item $installed -Force -ErrorAction SilentlyContinue
}
Write-Host "Webicam: виртуальная камера удалена."
