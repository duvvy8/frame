@echo off
setlocal
set "ROOT=%~dp0.."
if exist "%ROOT%\baseline-build.exit" del "%ROOT%\baseline-build.exit"
call "%~dp0build-baseline.cmd" >> "%ROOT%\baseline-build.log" 2>&1
if errorlevel 1 (
  >"%ROOT%\baseline-build.exit" echo 1
) else (
  >"%ROOT%\baseline-build.exit" echo 0
)
