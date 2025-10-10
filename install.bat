@echo off
REM NightForge Windows Installer
REM This batch file launches the PowerShell installer with elevated privileges

echo ===============================================
echo    NightForge Windows Installer
echo ===============================================
echo.
echo This installer will:
echo  - Download and install build dependencies
echo  - Compile NightForge from source
echo  - Optionally add NightScript to PATH
echo.
echo Press any key to continue...
pause >nul

REM Check for admin rights
net session >nul 2>&1
if %errorLevel% == 0 (
    echo Running with administrator privileges...
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
) else (
    echo Requesting administrator privileges...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process PowerShell -ArgumentList '-NoProfile -ExecutionPolicy Bypass -File \"%~dp0install.ps1\"' -Verb RunAs"
)

pause
