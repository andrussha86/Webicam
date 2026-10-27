@echo off
rem Double-click entry point: runs install.ps1 elevated (Windows offers no
rem "Run as administrator" for .ps1 files).
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile -ExecutionPolicy Bypass -NoExit -File \"%~dp0install.ps1\"'"
