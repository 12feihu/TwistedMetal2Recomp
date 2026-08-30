@echo off
REM Run a command with the MSVC x64 toolchain, VS-bundled CMake/Ninja and Git's
REM bash on PATH. Usage:  scripts\env.cmd cmake --build build --parallel
setlocal
set "VSROOT=G:\Tools\Visual Studio"
if not exist "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" (
  echo error: Visual Studio not found at "%VSROOT%" 1>&2
  exit /b 1
)
REM vcvars64.bat shells out to vswhere.exe; without the Installer dir on PATH it
REM prints a harmless "not recognized" line to stderr on every invocation.
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
set "PATH=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;C:\Program Files\Git\bin;C:\Program Files\Git\usr\bin;%PATH%"
REM Framework Python tools print em dashes; a cp1252 console kills them.
set "PYTHONUTF8=1"
%*
