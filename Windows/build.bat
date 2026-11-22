@echo off
rem Builds Search for Windows: one .exe, its icon, and WebView2's loader
rem beside it. Run from anywhere; it sets up the compiler itself.
setlocal
cd /d "%~dp0"

if not exist build\webview2\build\native\include\WebView2.h (
  echo The WebView2 SDK is missing. Fetching it once...
  if not exist build mkdir build
  powershell -NoProfile -Command "Invoke-WebRequest -UseBasicParsing -Uri 'https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.3485.44' -OutFile 'build\wv2.zip'"
  powershell -NoProfile -Command "Expand-Archive -Force -Path 'build\wv2.zip' -DestinationPath 'build\wv2tmp'"
  if not exist build\webview2 mkdir build\webview2
  xcopy /E /I /Y build\wv2tmp\build build\webview2\build >nul
  rmdir /S /Q build\wv2tmp
  del build\wv2.zip
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

echo Built build\Search.exe
exit /b 0

:fail
echo Build failed.
exit /b 1
