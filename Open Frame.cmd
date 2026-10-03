@echo off
call "%~dp0scripts\launch-frame.cmd" %*
if errorlevel 1 (
  echo.
  echo Frame is not ready yet. Check PROGRESS.md for the build status.
  pause
)
