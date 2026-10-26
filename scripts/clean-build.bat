@echo off
setlocal

cd /d "%~dp0"

for %%i in ("%~dp0..") do set PROJECT_ROOT=%%~fi
set BUILD_DIR=%PROJECT_ROOT%\build

if exist "%BUILD_DIR%" (
    echo Removing "%BUILD_DIR%"...
    rmdir /S /Q "%BUILD_DIR%"
    if exist "%BUILD_DIR%" (
        echo ERROR: Could not remove "%BUILD_DIR%".
        echo Close any process that may hold files inside it and retry.
        pause & exit /b 1
    )
)

call "%~dp0build.bat"
exit /b %ERRORLEVEL%