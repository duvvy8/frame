@echo off
setlocal
set "ROOT=%~dp0.."
set "PATH=%ROOT%\depot_tools;%PATH%"
set "DEPOT_TOOLS_WIN_TOOLCHAIN=0"
if not exist "%ROOT%\chromium\src\.git" (echo Source checkout missing & exit /b 1)
if not exist "%ROOT%\chromium\src\out\FrameBaseline\chrome.exe" (echo Build and verify the clean baseline first & exit /b 1)
pushd "%ROOT%\chromium\src"
set /p FRAME_REV=<"%ROOT%\CHROMIUM_REVISION"
for /f %%A in ('git rev-parse HEAD') do set "CURRENT_REV=%%A"
if not "%CURRENT_REV%"=="%FRAME_REV%" (echo Chromium revision does not match CHROMIUM_REVISION & popd & exit /b 1)
popd
python "%ROOT%\scripts\make_dev_icon.py"
if errorlevel 1 exit /b 1
python "%ROOT%\scripts\apply_frame.py"
if errorlevel 1 exit /b 1
python "%ROOT%\scripts\apply_native_shell.py"
if errorlevel 1 exit /b 1
python "%ROOT%\scripts\apply_native_appearance.py"
if errorlevel 1 exit /b 1
pushd "%ROOT%\chromium\src"
call gn.bat gen out\FrameBaseline --args="is_debug=false is_component_build=true symbol_level=0 blink_symbol_level=0 v8_symbol_level=0"
if errorlevel 1 (popd & exit /b 1)
call autoninja.bat -C out\FrameBaseline chrome
if errorlevel 1 (popd & exit /b 1)
copy /y out\FrameBaseline\chrome.exe out\FrameBaseline\Frame.exe >nul
if errorlevel 1 (popd & exit /b 1)
popd
