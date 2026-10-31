@echo off
rem ===============================================================================
rem        TRINH BIEN DICH 1-CLICK CLIENT TLBB 64-BIT (372 DRAGON / CY-SOURCE)
rem                   Tu dong kiem tra thu vien va Build x64
rem ===============================================================================
title TLBB 64-bit 1-Click Builder
color 0F

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

echo ===============================================================================
echo        TRINH BIEN DICH TU DONG CLIENT TLBB 64-BIT (372 DRAGON)
echo ===============================================================================
echo.
echo [*] Dang kiem tra toan bo moi truong va thu vien can thiet...
echo.

set "HAS_ERROR=0"

rem -------------------------------------------------------------------------------
rem 1. Kiem tra Visual Studio C++ Compiler (MSVC 2022 / 2019)
rem -------------------------------------------------------------------------------
echo [1/4] Kiem tra trinh bien dich Visual Studio C++ (MSVC x64)...
set "VS_PATH="

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VS_PATH=%%i"

if "%VS_PATH%"=="" if exist "C:\Program Files\Microsoft Visual Studio\2022\Community" set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community"
if "%VS_PATH%"=="" if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional" set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Professional"
if "%VS_PATH%"=="" if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise" set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
if "%VS_PATH%"=="" if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools" set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
if "%VS_PATH%"=="" if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools" set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\BuildTools"
if "%VS_PATH%"=="" if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools" set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools"
if "%VS_PATH%"=="" if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community" set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community"

if not "%VS_PATH%"=="" if exist "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" goto vs_ok

echo    -^> [THIEU] Khong tim thay Visual Studio C++ (MSVC x64)
echo       Yeu cau cai dat Visual Studio 2022 voi goi:
echo       "Desktop development with C++"
echo       (Lenh cai nhanh qua PowerShell: winget install Microsoft.VisualStudio.2022.Community --silent)
set "HAS_ERROR=1"
goto vs_done

:vs_ok
echo    -^> [OK] Da tim thay Visual Studio: "%VS_PATH%"

:vs_done

rem -------------------------------------------------------------------------------
rem 2. Kiem tra cong cu CMake va Ninja
rem -------------------------------------------------------------------------------
echo.
echo [2/4] Kiem tra cong cu dong goi CMake va Ninja...
set "CMAKE_BIN="
set "NINJA_BIN="

if not "%VS_PATH%"=="" (
    if exist "%VS_PATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_BIN=%VS_PATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if exist "%VS_PATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" set "NINJA_BIN=%VS_PATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
)

if "%CMAKE_BIN%"=="" (
    where cmake.exe >nul 2>&1
    if not errorlevel 1 set "CMAKE_BIN=cmake.exe"
)

if "%NINJA_BIN%"=="" (
    where ninja.exe >nul 2>&1
    if not errorlevel 1 set "NINJA_BIN=ninja.exe"
)

if not "%CMAKE_BIN%"=="" (
    echo    -^> [OK] CMake: san sang.
) else (
    echo    -^> [THIEU] Khong tim thay cmake.exe!
    echo       Vui long mo Visual Studio Installer va tich chon "C++ CMake tools for Windows".
    set "HAS_ERROR=1"
)

if not "%NINJA_BIN%"=="" (
    echo    -^> [OK] Ninja: san sang.
) else (
    echo    -^> [THIEU] Khong tim thay ninja.exe!
    echo       Vui long mo Visual Studio Installer va tich chon "C++ CMake tools for Windows".
    set "HAS_ERROR=1"
)

rem -------------------------------------------------------------------------------
rem 3. Kiem tra Thu vien DirectX 9 SDK x64 (d3d9.lib, d3dx9.lib, dxguid.lib)
rem -------------------------------------------------------------------------------
echo.
echo [3/4] Kiem tra thu vien do hoa DirectX 9 x64 SDK...
set "DX9_DIR=%ROOT%\Src\ThirdParty\DirectX9\Lib\x64"

if exist "%DX9_DIR%\d3d9.lib" if exist "%DX9_DIR%\d3dx9.lib" (
    echo    -^> [OK] Thu vien DirectX 9 x64: day du.
) else (
    echo    -^> [THIEU] Thu vien DirectX 9 x64 SDK trong Src\ThirdParty\DirectX9\Lib\x64
    echo       Can co cac file: d3d9.lib, d3dx9.lib, dxguid.lib
    set "HAS_ERROR=1"
)

rem -------------------------------------------------------------------------------
rem 4. Kiem tra ma nguon Client
rem -------------------------------------------------------------------------------
echo.
echo [4/4] Kiem tra cay ma nguon Client...
if exist "%ROOT%\Src\CMakeLists.txt" if exist "%ROOT%\Src\Client\WXClient" (
    echo    -^> [OK] Cau truc ma nguon Client 372 Dragon: hop le.
) else (
    echo    -^> [THIEU] Khong tim thay thu muc ma nguon Src\Client!
    set "HAS_ERROR=1"
)

rem -------------------------------------------------------------------------------
rem TONG KET VA QUYET DINH BIEN DICH
rem -------------------------------------------------------------------------------
echo.
echo ===============================================================================

if "%HAS_ERROR%"=="1" (
    color 0C
    echo [CANH BAO] PHAT HIEN THIEU THU VIEN HOAC CONG CU!
    echo Vui long cai dat bo sung cac thanh phan bao [THIEU] o tren truoc khi build.
    echo ===============================================================================
    echo.
    echo Nhan phim bat ky de thoat...
    pause >nul
    exit /b 1
)

color 0A
echo [THANH CONG] TOAN BO MOI TRUONG VA THU VIEN DA DAY DU VA SAN SANG!
echo ===============================================================================
echo.
echo [*] Tu dong bat dau qua trinh bien dich Release x64...
ping -n 3 127.0.0.1 >nul
color 0F

set "VS=%VS_PATH%"
call "%ROOT%\Tools\build\build.cmd" x64 public-x64 Release

if errorlevel 1 (
    color 0C
    echo.
    echo ===============================================================================
    echo [LOI] Qua trinh bien dich gap su co.
    echo Kiem tra log chi tiet tai: "%ROOT%\Build\cmake\public-x64"
    echo ===============================================================================
    pause
    exit /b 1
)

echo.
echo ===============================================================================
echo                     BIEN DICH HOAN TAT THANH CONG!
echo ===============================================================================
echo.
echo [*] Dang dong bo Game.exe va cac thu vien DLL sang Play\Bin64...

if not exist "%ROOT%\Play\Bin64" mkdir "%ROOT%\Play\Bin64"

if exist "%ROOT%\Build\cmake\public-x64\bin\Game.exe" (
    copy /y "%ROOT%\Build\cmake\public-x64\bin\Game.exe" "%ROOT%\Play\Bin64\" >nul
    echo    -^> Da cap nhat Game.exe
)
if exist "%ROOT%\Build\cmake\public-x64\bin\Render.dll" (
    copy /y "%ROOT%\Build\cmake\public-x64\bin\*.dll" "%ROOT%\Play\Bin64\" >nul
    echo    -^> Da cap nhat toan bo DLL (Render.dll, OgreMain.dll,...)
)
if exist "%ROOT%\Build\cmake\public-x64\bin\TlbbViewer.exe" (
    copy /y "%ROOT%\Build\cmake\public-x64\bin\TlbbViewer.exe" "%ROOT%\Play\Bin64\" >nul
    echo    -^> Da cap nhat TlbbViewer.exe
)

rem Kiem tra junction Play\Data
if not exist "%ROOT%\Play\Data" (
    echo [*] Dang khoi tao lien ket thu muc Play\Data...
    powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\Publication\Setup-Play.ps1" -Root "%ROOT%" >nul 2>&1
)

color 0A
echo.
echo ===============================================================================
echo  BO CAI DA SAN SANG DE CHIA SE CHO ANH EM!
echo  - File khoi dong Game:   %ROOT%\Run_Game.bat
echo  - File xem 3D Offline:   %ROOT%\Run_Viewer.bat
echo  - Thu muc chay thuc thi: %ROOT%\Play\Bin64
echo ===============================================================================
echo.
echo Nhan phim bat ky de ket thuc...
pause >nul
