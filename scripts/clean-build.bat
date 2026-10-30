@echo off
setlocal
cd /d "%~dp0"
call "%~dp0clean.bat" || exit /b %ERRORLEVEL%
call "%~dp0build.bat"
exit /b %ERRORLEVEL%