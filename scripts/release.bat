@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0"

for %%i in ("%~dp0..") do set "PROJECT_ROOT=%%~fi"
set "SRC_EXE=%PROJECT_ROOT%\build\ttptt.exe"
set "SRC_README=%PROJECT_ROOT%\README.md"
set "SRC_SAMPLES=%PROJECT_ROOT%\sample-tim"
set "SRC_SAMPLES_ZIP=%SRC_SAMPLES%\sample-tim.zip"
set "RELEASE_ROOT=%PROJECT_ROOT%\releases"

REM --- read version (first line, spaces stripped) ---
if not exist "%PROJECT_ROOT%\version.txt" (
    echo ERROR: version.txt not found at "%PROJECT_ROOT%"
    pause & exit /b 1
)
set "VERSION="
set /p VERSION=<"%PROJECT_ROOT%\version.txt"
set "VERSION=%VERSION: =%"
if "%VERSION%"=="" (
    echo ERROR: version.txt is empty.
    pause & exit /b 1
)

set "NAME=tim-to-picture-to-tim-v%VERSION%-win32-static"
set "DEST=%RELEASE_ROOT%\%NAME%"
set "ZIP_PATH=%RELEASE_ROOT%\%NAME%.zip"
set "DEST_SAMPLES=%DEST%\sample-tim"

REM --- sanity ---
if not exist "%SRC_EXE%" (
    echo ERROR: %SRC_EXE% not found. Build first ^(scripts\build.bat^).
    pause & exit /b 1
)
if not exist "%SRC_README%" (
    echo ERROR: %SRC_README% not found.
    pause & exit /b 1
)

REM --- create release dir ---
if not exist "%RELEASE_ROOT%" mkdir "%RELEASE_ROOT%"
if not exist "%DEST%" mkdir "%DEST%"

REM --- README.md (always overwrite) ---
copy /Y "%SRC_README%" "%DEST%\README.md" >nul
if errorlevel 1 (
    echo ERROR: failed to copy README.md
    pause & exit /b 1
)
echo [ok] README.md

REM --- sample-tim (extract sample-tim.zip into release sample-tim) ---
if exist "%SRC_SAMPLES%" (
    if not exist "%SRC_SAMPLES_ZIP%" (
        echo ERROR: %SRC_SAMPLES_ZIP% not found.
        pause & exit /b 1
    )

    if exist "%DEST_SAMPLES%" rmdir /S /Q "%DEST_SAMPLES%"
    mkdir "%DEST_SAMPLES%"
    if not exist "%DEST_SAMPLES%" (
        echo ERROR: failed to create "%DEST_SAMPLES%"
        pause & exit /b 1
    )

    where tar >nul 2>&1
    if errorlevel 1 (
        echo [--] tar not found, trying PowerShell Expand-Archive...
        powershell -NoProfile -ExecutionPolicy Bypass -Command ^
            "Expand-Archive -LiteralPath '%SRC_SAMPLES_ZIP%' -DestinationPath '%DEST_SAMPLES%' -Force"
        if errorlevel 1 (
            echo ERROR: failed to extract sample-tim.zip with PowerShell
            pause & exit /b 1
        )
    ) else (
        tar -xf "%SRC_SAMPLES_ZIP%" -C "%DEST_SAMPLES%"
        if errorlevel 1 (
            echo ERROR: failed to extract sample-tim.zip with tar
            pause & exit /b 1
        )
    )
    echo [ok] sample-tim extracted
) else (
    echo [--] sample-tim folder not found, skipped
)

REM --- ttptt.exe (copy + UPX brute) ---
copy /Y "%SRC_EXE%" "%DEST%\ttptt.exe" >nul
if errorlevel 1 (
    echo ERROR: failed to copy ttptt.exe
    pause & exit /b 1
)
for %%F in ("%DEST%\ttptt.exe") do set "EXE_SIZE_RAW=%%~zF"
echo [ok] ttptt.exe copied ^(!EXE_SIZE_RAW! bytes^)

where upx >nul 2>&1
if errorlevel 1 (
    echo [--] upx not found in PATH, skipping compression
) else (
    REM echo [..] upx --brute ^(may take several minutes^)...
    REM Decompress if already packed (from a previous UPX=ON build).
    REM Errors are ignored: if the file isn't packed, upx -d exits non-zero.
    upx -d "%DEST%\ttptt.exe" >nul 2>&1
    upx --best --force "%DEST%\ttptt.exe"
    if errorlevel 1 (
        echo ERROR: upx --brute failed
        pause & exit /b 1
    )
    for %%F in ("%DEST%\ttptt.exe") do set "EXE_SIZE_PACKED=%%~zF"
    echo [ok] ttptt.exe compressed ^(!EXE_SIZE_PACKED! bytes, was !EXE_SIZE_RAW!^)
)

REM --- zip packaging ---
if exist "%ZIP_PATH%" del /Q "%ZIP_PATH%"

set "ZIP_RC=1"

where tar >nul 2>&1
if errorlevel 1 goto :zip_powershell

pushd "%RELEASE_ROOT%"
tar -a -c -f "%NAME%.zip" "%NAME%"
set "ZIP_RC=%ERRORLEVEL%"
popd
goto :zip_check

:zip_powershell
echo [--] tar not found, trying PowerShell Compress-Archive...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "Compress-Archive -Path '%RELEASE_ROOT%\%NAME%' -DestinationPath '%ZIP_PATH%' -Force"
set "ZIP_RC=%ERRORLEVEL%"

:zip_check
if not "%ZIP_RC%"=="0" (
    echo WARNING: zip packaging failed ^(exit %ZIP_RC%^)
    echo [!!] zip skipped
) else (
    for %%F in ("%ZIP_PATH%") do set "ZIP_SIZE=%%~zF"
    echo [ok] %NAME%.zip ^(!ZIP_SIZE! bytes^)
)

echo.
echo RELEASE READY
echo   folder: %DEST%
if exist "%ZIP_PATH%" echo   zip:    %ZIP_PATH%
echo.
echo Contents:
dir /b "%DEST%"

REM Uncomment to open the folder in Explorer after release:
REM start "" "%DEST%"

pause
exit /b 0