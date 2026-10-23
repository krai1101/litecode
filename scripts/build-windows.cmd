@echo off
setlocal

set "LITECODE_CONFIGURATION=%~1"
if "%LITECODE_CONFIGURATION%"=="" set "LITECODE_CONFIGURATION=Debug"

if "%~2"=="" (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-windows.ps1" -Configuration "%LITECODE_CONFIGURATION%"
) else (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-windows.ps1" -Configuration "%LITECODE_CONFIGURATION%" -QtRoot "%~2"
)

exit /b %ERRORLEVEL%
