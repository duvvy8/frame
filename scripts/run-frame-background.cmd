@echo off
setlocal
set "ROOT=%~dp0.."
if exist "%ROOT%\frame-build.exit" del "%ROOT%\frame-build.exit"
call "%~dp0build-frame.cmd" >> "%ROOT%\frame-build.log" 2>&1
if errorlevel 1 (
  >"%ROOT%\frame-build.exit" echo 1
) else (
  >"%ROOT%\frame-build.exit" echo 0
)
