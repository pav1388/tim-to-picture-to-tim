@echo off
setlocal

cd /d "%~dp0"

REM Absolute paths: %~dp0 ends with a backslash; %%~fi normalizes ".."
for %%i in ("%~dp0..") do set PROJECT_ROOT=%%~fi
set BUILD_DIR=%PROJECT_ROOT%\build
set QT_DIR=%PROJECT_ROOT%\qt-5.6.3-static
set EXE_OUT=%PROJECT_ROOT%\ttptt.exe

REM --- sanity checks ---
where cmake >nul 2>&1 || (
    echo ERROR: cmake is not in PATH.
    pause & exit /b 1
)
where gcc >nul 2>&1 || (
    echo ERROR: gcc is not in PATH ^(expected mingw492_32\bin^).
    pause & exit /b 1
)
where mingw32-make >nul 2>&1 || (
    echo ERROR: mingw32-make is not in PATH ^(expected mingw492_32\bin^).
    pause & exit /b 1
)
if not exist "%QT_DIR%" (
    echo ERROR: Qt SDK not found at "%QT_DIR%"
    pause & exit /b 1
)
if not exist "%PROJECT_ROOT%\CMakeLists.txt" (
    echo ERROR: CMakeLists.txt not found at "%PROJECT_ROOT%"
    pause & exit /b 1
)

REM --- toolchain banner (helps spot a wrong MinGW in PATH) ---
echo Toolchain:
where gcc
gcc --version | findstr /R "^gcc"
echo.

REM --- configure (re-uses the cache if present) ---
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

REM Dev build: no UPX. Use scripts\release.bat for the compressed release.

cmake -G "MinGW Makefiles" ^
      -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
      -DTTPTT_QT_DIR="%QT_DIR%" ^
      -DTTPTT_UPX=OFF ^
      "%PROJECT_ROOT%"
if errorlevel 1 (
    echo ERROR: CMake configuration failed.
    pause & exit /b 1
)

REM --- build ---
REM For verbose output, replace the next line with: mingw32-make -j4 VERBOSE=1
mingw32-make -j4
if errorlevel 1 (
    echo ERROR: Build failed.
    pause & exit /b 1
)

REM --- copy the exe next to the project root ---
if not exist "ttptt.exe" (
    echo WARNING: ttptt.exe not produced by the build.
    pause & exit /b 1
)

echo.
echo BUILD SUCCESSFUL
echo Output: %EXE_OUT%
pause
exit /b 0