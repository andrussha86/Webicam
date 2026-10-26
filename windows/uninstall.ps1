# Unregisters the Webicam virtual camera (run as Administrator).
$installed = Join-Path (Join-Path $env:ProgramFiles "Webicam") "webicam_vcam.dll"
if (Test-Path $installed) {
    Start-Process regsvr32.exe -ArgumentList "/u /s `"$installed`"" -Wait
    Remove-Item $installed -Force -ErrorAction SilentlyContinue
}
Write-Host "Webicam: виртуальная камера удалена."
