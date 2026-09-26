@echo off
setlocal
pushd "%~dp0.."
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0release-windows.ps1" %*
set "LITECODE_RELEASE_EXIT=%ERRORLEVEL%"
popd
exit /b %LITECODE_RELEASE_EXIT%
