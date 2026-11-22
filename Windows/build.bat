@echo off
rem Builds Search for Windows: Search.exe and the signed CEF runtime it carries.
rem Run from anywhere; it sets up the compiler and downloads CEF once into a
rem cache outside the shipping folder.
setlocal EnableDelayedExpansion
cd /d "%~dp0"

set "CEF_VERSION=158.0.3+g8c29be2+chromium-158.0.8127.18"
set "CEF_MINIMAL_FILE=cef_binary_%CEF_VERSION%_windows64_minimal.tar.bz2"
set "CEF_MINIMAL_SHA1=a8634573a10fb514fb9e8bc9fcf39016a2fb17bd"
set "CEF_MINIMAL_URL=https://cef-builds.spotifycdn.com/%CEF_MINIMAL_FILE%"
set "CEF_SIGNED_FILE=cef_binary_%CEF_VERSION%_windows64_signed.tar.xz"
set "CEF_SIGNED_SHA1=b6a40bde3f118e61b58be73f546ab641ae586d4c"
set "CEF_SIGNED_URL=https://cef-builds.spotifycdn.com/%CEF_SIGNED_FILE%"

set "CEF_CACHE=%LOCALAPPDATA%\SearchCEF\cache"
set "CEF_MINIMAL_DIR=%CEF_CACHE%\cef_binary_%CEF_VERSION%_windows64_minimal"
rem The signed archive has no top-level version folder: it unpacks straight
rem to Release, so give it its own directory instead of unpacking into the
rem shared cache root.
set "CEF_SIGNED_DIR=%CEF_CACHE%\signed"

if not exist "%CEF_CACHE%" mkdir "%CEF_CACHE%" || goto :fail

rem ---- download and verify the two pinned archives ----
call :get_cef "%CEF_MINIMAL_URL%" "%CEF_CACHE%\%CEF_MINIMAL_FILE%" "%CEF_MINIMAL_SHA1%" || goto :fail
call :get_cef "%CEF_SIGNED_URL%" "%CEF_CACHE%\%CEF_SIGNED_FILE%" "%CEF_SIGNED_SHA1%" || goto :fail

rem ---- unpack them once ----
if not exist "%CEF_MINIMAL_DIR%\.unpacked" (
  echo Unpacking the CEF headers and import library...
  if exist "%CEF_MINIMAL_DIR%" rmdir /S /Q "%CEF_MINIMAL_DIR%"
  tar -xf "%CEF_CACHE%\%CEF_MINIMAL_FILE%" -C "%CEF_CACHE%"
  if errorlevel 1 goto :fail
  echo %CEF_MINIMAL_SHA1%> "%CEF_MINIMAL_DIR%\.unpacked"
)
if not exist "%CEF_SIGNED_DIR%\.unpacked" (
  echo Unpacking the signed CEF runtime...
  if exist "%CEF_SIGNED_DIR%" rmdir /S /Q "%CEF_SIGNED_DIR%"
  mkdir "%CEF_SIGNED_DIR%" || goto :fail
  tar -xf "%CEF_CACHE%\%CEF_SIGNED_FILE%" -C "%CEF_SIGNED_DIR%"
  if errorlevel 1 goto :fail
  echo %CEF_SIGNED_SHA1%> "%CEF_SIGNED_DIR%\.unpacked"
)
if not exist "%CEF_SIGNED_DIR%\Release\libcef.dll" (
  echo The signed CEF runtime did not unpack as expected.
  goto :fail
)

rem ---- compiler ----
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
  call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
)
if errorlevel 1 (
  echo Visual Studio 2022 with the C++ desktop tools is required.
  goto :fail
)

rem ---- build the C++ wrapper against the same CEF version ----
rem The signed package is runtime-only, so the wrapper is compiled here from
rem the matching minimal package's sources.
echo Compiling libcef_dll_wrapper...
if exist "%CEF_CACHE%\wrapobj" rmdir /S /Q "%CEF_CACHE%\wrapobj"
mkdir "%CEF_CACHE%\wrapobj" || goto :fail
if exist "%CEF_CACHE%\wrapper.rsp" del "%CEF_CACHE%\wrapper.rsp"
pushd "%CEF_MINIMAL_DIR%" || goto :fail
for /R "libcef_dll\wrapper" %%F in (*.cc) do echo "%%F">>"%CEF_CACHE%\wrapper.rsp"
cl /nologo /MP /c /EHsc /std:c++20 /O1 /W0 ^
  /DUNICODE /D_UNICODE /DNOMINMAX /DWRAPPING_CEF_SHARED ^
  /I. /Iinclude ^
  @"%CEF_CACHE%\wrapper.rsp" ^
  /Fo"%CEF_CACHE%\wrapobj\\"
set "WRAPPER_RESULT=%errorlevel%"
popd
if not "%WRAPPER_RESULT%"=="0" goto :fail

if exist "%CEF_CACHE%\wrapperlib.rsp" del "%CEF_CACHE%\wrapperlib.rsp"
for %%F in ("%CEF_CACHE%\wrapobj\*.obj") do echo "%%F">>"%CEF_CACHE%\wrapperlib.rsp"
lib /nologo /out:"%CEF_CACHE%\libcef_dll_wrapper.lib" @"%CEF_CACHE%\wrapperlib.rsp"
if errorlevel 1 goto :fail

rem ---- put the signed runtime beside the app ----
echo Copying the signed CEF runtime into build...
if not exist build mkdir build
xcopy /E /I /Y "%CEF_SIGNED_DIR%\Release\*" "build\" >nul
if errorlevel 1 goto :fail

rem ---- build the app ----
if not exist build\obj mkdir build\obj
rc /nologo /fo build\obj\Search.res /I src src\Search.rc
if errorlevel 1 goto :fail

cl /nologo /EHsc /std:c++20 /O2 /W3 /DUNICODE /D_UNICODE /DNOMINMAX ^
  /I"%CEF_MINIMAL_DIR%" ^
  /Fo"build\obj\\" /Fd"build\obj\\" ^
  src\Address.cpp src\App.cpp src\Data.cpp src\Input.cpp src\Internal.cpp ^
  src\Json.cpp src\Main.cpp src\Paint.cpp src\Shield.cpp src\Store.cpp ^
  src\Tabs.cpp src\Theme.cpp src\WebView.cpp src\Window.cpp ^
  build\obj\Search.res ^
  /link /SUBSYSTEM:WINDOWS /OUT:build\Search.exe ^
  "%CEF_MINIMAL_DIR%\Release\libcef.lib" ^
  "%CEF_CACHE%\libcef_dll_wrapper.lib" ^
  user32.lib gdi32.lib gdiplus.lib shell32.lib ole32.lib oleaut32.lib shlwapi.lib dwmapi.lib advapi32.lib version.lib ^
  /MANIFEST:EMBED /MANIFESTINPUT:src\Search.manifest
if errorlevel 1 goto :fail

echo Built build\Search.exe with signed CEF %CEF_VERSION%.
powershell -NoProfile -Command "$p='build'; $items=Get-ChildItem -LiteralPath $p -Recurse -File; $mb=($items | Measure-Object -Property Length -Sum).Sum / 1MB; '{0:N1} MB in {1} files under {2}' -f $mb,$items.Count,(Resolve-Path $p)"
exit /b 0

:get_cef
set "GET_URL=%~1"
set "GET_FILE=%~2"
set "GET_SHA1=%~3"
set "GET_HASH="
if exist "%GET_FILE%" (
  for /f "delims=" %%H in ('powershell -NoProfile -Command "(Get-FileHash -LiteralPath '%GET_FILE%' -Algorithm SHA1).Hash.ToLowerInvariant()"') do set "GET_HASH=%%H"
)
if /I "%GET_HASH%"=="%GET_SHA1%" exit /b 0
if exist "%GET_FILE%" del "%GET_FILE%"
echo Fetching %~nx2 once...
powershell -NoProfile -Command "$ProgressPreference='SilentlyContinue'; [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -UseBasicParsing -Uri '%GET_URL%' -OutFile '%GET_FILE%'"
if errorlevel 1 exit /b 1
set "GET_HASH="
for /f "delims=" %%H in ('powershell -NoProfile -Command "(Get-FileHash -LiteralPath '%GET_FILE%' -Algorithm SHA1).Hash.ToLowerInvariant()"') do set "GET_HASH=%%H"
if /I not "%GET_HASH%"=="%GET_SHA1%" (
  echo Hash mismatch for %~nx2: expected %GET_SHA1%, got %GET_HASH%.
  exit /b 1
)
exit /b 0

:fail
echo Build failed.
exit /b 1
