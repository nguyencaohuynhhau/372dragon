@echo off
rem CY-Source build: CMake + Ninja inside the MSVC 2022 Build Tools environment.
rem Usage:
rem   build.cmd ARCH NAME [CONFIG] ["comp1;comp2;..."] [TARGET]
rem     ARCH   = x64 or x86
rem     NAME   = build tree folder under Build\cmake (e.g. ogre-x64)
rem     CONFIG = Release (default), Debug, RelWithDebInfo
rem   Example:
rem   build.cmd x64 ogre-x64 Release "zlib;zziplib;freetype;devil;ogremain;ogre_d3d9"
rem NOTE: keep this file ASCII only (cmd.exe misparses UTF-8 comments).
setlocal
set "ARCH=%~1"
if "%ARCH%"=="" set "ARCH=x64"
set "NAME=%~2"
if "%NAME%"=="" set "NAME=%ARCH%-release"
set "CFG=%~3"
if "%CFG%"=="" set "CFG=Release"
rem CYS-BUILDCFG 18/09: reject an unknown CONFIG instead of passing it to CMake.
rem Every build in the project used "build.cmd x64 NAME cys_wxclient", which put the TARGET
rem name into CONFIG: CMAKE_BUILD_TYPE became "cys_wxclient", an unknown type, so CMake applied
rem NO optimisation flags at all (CMAKE_CXX_FLAGS was just /DWIN32 /D_WINDOWS /EHsc, and the
rem /O2 /Ob2 /DNDEBUG of CMAKE_CXX_FLAGS_RELEASE was never used). The build still succeeded and
rem still produced a working Game.exe, so nothing ever flagged it. Correct form:
rem   build.cmd x64 NAME Release "" cys_wxclient
if /I "%CFG%"=="Release" goto cfg_ok
if /I "%CFG%"=="Debug" goto cfg_ok
if /I "%CFG%"=="RelWithDebInfo" goto cfg_ok
if /I "%CFG%"=="MinSizeRel" goto cfg_ok
echo(
echo [build.cmd] CONFIG khong hop le: "%CFG%"
echo   Cu phap: build.cmd ARCH NAME [CONFIG] ["comp1;comp2"] [TARGET]
echo   CONFIG chi nhan: Release ^| Debug ^| RelWithDebInfo ^| MinSizeRel
echo   Neu "%CFG%" la TEN TARGET thi dat no o vi tri thu 5:
echo       build.cmd %ARCH% %NAME% Release "" %CFG%
echo(
exit /b 2
:cfg_ok
set "COMPS=%~4"
set "TARGET=%~5"
rem Detect Visual Studio 2022 installation
set "VS="
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" set "VS=C:\Program Files\Microsoft Visual Studio\2022\Professional"
if "%VS%"=="" if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" set "VS=C:\Program Files\Microsoft Visual Studio\2022\Community"
if "%VS%"=="" if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" set "VS=C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
if "%VS%"=="" if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
if "%VS%"=="" (
    echo [build.cmd] Khong tim thay Visual Studio 2022
    exit /b 1
)
call "%VS%\VC\Auxiliary\Build\vcvarsall.bat" %ARCH% >nul
if errorlevel 1 exit /b 1
set "CMAKE=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" set "CMAKE=cmake"
set "NINJA=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
set "ROOT=%~dp0..\.."
set "BLD=%ROOT%\Build\cmake\%NAME%"
if "%COMPS%"=="" goto cfg_all
"%CMAKE%" -S "%ROOT%\Src" -B "%BLD%" -G Ninja "-DCMAKE_MAKE_PROGRAM=%NINJA%" -DCMAKE_BUILD_TYPE=%CFG% "-DCYS_COMPONENTS=%COMPS%"
goto cfg_done
:cfg_all
"%CMAKE%" -S "%ROOT%\Src" -B "%BLD%" -G Ninja "-DCMAKE_MAKE_PROGRAM=%NINJA%" -DCMAKE_BUILD_TYPE=%CFG%
:cfg_done
if errorlevel 1 exit /b 1
if "%TARGET%"=="" goto build_all
"%CMAKE%" --build "%BLD%" --target %TARGET% -- -k 0
exit /b %ERRORLEVEL%
:build_all
"%CMAKE%" --build "%BLD%" -- -k 0
exit /b %ERRORLEVEL%
