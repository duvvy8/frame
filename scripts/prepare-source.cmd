@echo off
setlocal
set "ROOT=%~dp0.."
set "PATH=%ROOT%\depot_tools;%PATH%"
set "DEPOT_TOOLS_WIN_TOOLCHAIN=0"
if not exist "%ROOT%\depot_tools\fetch.bat" (
  echo Clone depot_tools first. See BUILD_WINDOWS.md.
  exit /b 1
)
if not exist "%ROOT%\chromium\src\.git" (
  if not exist "%ROOT%\chromium" mkdir "%ROOT%\chromium"
  pushd "%ROOT%\chromium"
  call fetch.bat --no-history chromium
  if errorlevel 1 (popd & exit /b 1)
  popd
)
pushd "%ROOT%\chromium\src"
set /p FRAME_REV=<"%ROOT%\CHROMIUM_REVISION"
call git fetch origin %FRAME_REV% --depth=1
if errorlevel 1 (popd & exit /b 1)
call git checkout --detach %FRAME_REV%
if errorlevel 1 (popd & exit /b 1)
call gclient.bat sync -D
if errorlevel 1 (popd & exit /b 1)
call git rev-parse HEAD
popd
