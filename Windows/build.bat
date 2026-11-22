@echo off
rem Builds Search for Windows: Search.exe and the WebView2 runtime it carries.
rem Run from anywhere; it sets up the compiler and downloads what it needs.
setlocal
cd /d "%~dp0"

set "WV2_SDK_VERSION=1.0.3485.44"
set "WV2_RUNTIME_VERSION=158.0.4370.33"
set "WV2_RUNTIME_URL=https://msedge.sf.dl.delivery.mp.microsoft.com/filestreamingservice/files/ecc2761e-1380-46ee-9552-995b039e4e47/Microsoft.WebView2.FixedVersionRuntime.%WV2_RUNTIME_VERSION%.x64.cab"

if not exist build\webview2\build\native\include\WebView2.h (
  echo The WebView2 SDK is missing. Fetching it once...
  if not exist build mkdir build
  powershell -NoProfile -Command "Invoke-WebRequest -UseBasicParsing -Uri 'https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/%WV2_SDK_VERSION%' -OutFile 'build\wv2.zip'"
  if errorlevel 1 goto :fail
  powershell -NoProfile -Command "Expand-Archive -Force -Path 'build\wv2.zip' -DestinationPath 'build\wv2tmp'"
  if errorlevel 1 goto :fail
  if not exist build\webview2 mkdir build\webview2
  xcopy /E /I /Y build\wv2tmp\build build\webview2\build >nul
  if errorlevel 1 goto :fail
  rmdir /S /Q build\wv2tmp
  del build\wv2.zip
)

if not exist build\runtime\msedgewebview2.exe (
  if not exist build\wv2runtime.cab (
    echo Fetching the bundled WebView2 runtime once...
    powershell -NoProfile -Command "Invoke-WebRequest -UseBasicParsing -Uri '%WV2_RUNTIME_URL%' -OutFile 'build\wv2runtime.cab'"
    if errorlevel 1 goto :fail
  )
  if exist build\runtime-tmp rmdir /S /Q build\runtime-tmp
  mkdir build\runtime-tmp
  expand build\wv2runtime.cab -F:* build\runtime-tmp >nul
  if errorlevel 1 goto :fail
  if not exist build\runtime mkdir build\runtime
  xcopy /E /I /Y "build\runtime-tmp\Microsoft.WebView2.FixedVersionRuntime.%WV2_RUNTIME_VERSION%.x64\*" build\runtime >nul
  if errorlevel 1 goto :fail
  rmdir /S /Q build\runtime-tmp
  del build\wv2runtime.cab

  rem Windows 10 needs these grants for the renderer's AppContainer.
  icacls build\runtime /grant "*S-1-15-2-2:(OI)(CI)(RX)" /T /Q >nul 2>&1
  icacls build\runtime /grant "*S-1-15-2-1:(OI)(CI)(RX)" /T /Q >nul 2>&1
)

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
  call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
)

if not exist build\obj mkdir build\obj

rc /nologo /fo build\obj\Search.res /I src src\Search.rc
if errorlevel 1 goto :fail

cl /nologo /EHsc /std:c++20 /O2 /W3 /DUNICODE /D_UNICODE /DNOMINMAX ^
  /I"build\webview2\build\native\include" ^
  /Fo"build\obj\\" /Fd"build\obj\\" ^
  src\Address.cpp src\App.cpp src\Data.cpp src\Input.cpp src\Internal.cpp ^
  src\Json.cpp src\Main.cpp src\Paint.cpp src\Shield.cpp src\Store.cpp ^
  src\Tabs.cpp src\Theme.cpp src\WebView.cpp src\Window.cpp ^
  build\obj\Search.res ^
  /link /SUBSYSTEM:WINDOWS /OUT:build\Search.exe ^
  build\webview2\build\native\x64\WebView2LoaderStatic.lib ^
  user32.lib gdi32.lib gdiplus.lib shell32.lib ole32.lib oleaut32.lib shlwapi.lib dwmapi.lib advapi32.lib version.lib ^
  /MANIFEST:EMBED /MANIFESTINPUT:src\Search.manifest
if errorlevel 1 goto :fail

echo Built build\Search.exe with the WebView2 runtime in build\runtime
exit /b 0

:fail
echo Build failed.
exit /b 1
