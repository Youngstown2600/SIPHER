@echo off
setlocal EnableExtensions
set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=win10"
set "ROOT=%~dp0"
set "BASH=C:\msys64\usr\bin\bash.exe"
if not exist "%BASH%" (
  echo ERROR: MSYS2 was not found at C:\msys64.
  echo Install MSYS2, then run this file again.
  exit /b 2
)
pushd "%ROOT%" || exit /b 2
"%BASH%" -lc "export MSYSTEM=MINGW64; export MINGW_PREFIX=/mingw64; export PATH=/mingw64/bin:/usr/bin:$PATH; ./windows/build-portable.sh %TARGET%"
set "RC=%ERRORLEVEL%"
popd
exit /b %RC%
