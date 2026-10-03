@echo off
setlocal
set "ROOT=%~dp0.."
set "EXE=%ROOT%\chromium\src\out\FrameBaseline\Frame.exe"
if not exist "%EXE%" (echo Frame binary missing & exit /b 1)
if not exist "%LOCALAPPDATA%\Frame Dev\User Data" mkdir "%LOCALAPPDATA%\Frame Dev\User Data"
"%EXE%" --user-data-dir="%LOCALAPPDATA%\Frame Dev\User Data" %*
