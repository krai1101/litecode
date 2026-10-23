@echo off
setlocal

set "LITECODE_EXE=%~dp0build\release\src\app\LiteCode.exe"
set "LITECODE_QT=%~dp0build\release\src\app\Qt6Core.dll"

if not exist "%LITECODE_EXE%" (
  echo LiteCode Release has not been built yet.
  echo Run: scripts\build-windows.cmd Release
  exit /b 1
)

if not exist "%LITECODE_QT%" (
  echo The Qt runtime is missing. Redeploying it with a Release build...
  call "%~dp0scripts\build-windows.cmd" Release
  if errorlevel 1 exit /b %ERRORLEVEL%
)

pushd "%~dp0build\release\src\app"
start "" "%LITECODE_EXE%"
set "LITECODE_START_ERROR=%ERRORLEVEL%"
popd
exit /b %LITECODE_START_ERROR%
