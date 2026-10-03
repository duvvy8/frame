@echo off
setlocal
set "ROOT=%~dp0.."
set "PATH=%ROOT%\depot_tools;%PATH%"
set "DEPOT_TOOLS_WIN_TOOLCHAIN=0"
if not exist "%ROOT%\chromium\src\.git" (echo Source checkout missing & exit /b 1)
pushd "%ROOT%\chromium\src"
set /p FRAME_REV=<"%ROOT%\CHROMIUM_REVISION"
for /f %%A in ('git rev-parse HEAD') do set "CURRENT_REV=%%A"
if not "%CURRENT_REV%"=="%FRAME_REV%" (echo Chromium revision does not match CHROMIUM_REVISION & popd & exit /b 1)
call git diff --quiet -- chrome/app/theme/chromium/BRANDING chrome/app/chromium_strings.grd chrome/app/settings_chromium_strings.grdp chrome/app/theme/chromium/win/chromium.ico
if errorlevel 1 (echo Chromium branding tree is modified; baseline must be built first & popd & exit /b 1)
call gn.bat gen out\FrameBaseline --args="is_debug=false is_component_build=true symbol_level=0 blink_symbol_level=0 v8_symbol_level=0"
if errorlevel 1 (popd & exit /b 1)
call autoninja.bat -C out\FrameBaseline chrome
if errorlevel 1 (popd & exit /b 1)
popd
