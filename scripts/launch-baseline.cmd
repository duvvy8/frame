@echo off
setlocal
set "ROOT=%~dp0.."
set "EXE=%ROOT%\chromium\src\out\FrameBaseline\chrome.exe"
if not exist "%EXE%" (echo Baseline binary missing & exit /b 1)
"%EXE%" --user-data-dir="%ROOT%\profiles\Baseline" https://example.com
